#include "package/call_graph.h"

#include "analysis/argument_binding.h"
#include "analysis/cg/on_the_fly.h"
#include "analysis/python/language_features.h"
#include "analysis/python/protocol.h"
#include "analysis/pta/andersen.h"
#include "analysis/pta/semantic_pta.h"

#include <algorithm>
#include <array>
#include <deque>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cpygraph::package {
namespace {

constexpr FunctionId kFirstFunctionId = 1;
constexpr PTAContextId kFirstPTAContextId = 1;
constexpr std::size_t kPTACallStringDepth = 2;
constexpr std::string_view kSuperBuiltinName = "super";
constexpr std::string_view kClassMethodBuiltinName = "classmethod";
constexpr std::string_view kStaticMethodBuiltinName = "staticmethod";
constexpr std::string_view kCallSpecialMethodName = "__call__";
constexpr std::string_view kInitSpecialMethodName = "__init__";
constexpr std::string_view kPropertyBuiltinName = "property";
constexpr std::string_view kPropertySetterMethodName = "setter";
constexpr std::string_view kGetAttributeSpecialMethodName = "__getattribute__";
constexpr std::string_view kMetaclassKeywordName = "metaclass";
constexpr FieldId kSummaryFieldId = 0U;

struct QualifiedSymbol {
    std::string module;
    std::string name;

    bool operator==(const QualifiedSymbol& other) const noexcept {
        return module == other.module && name == other.name;
    }
};

struct QualifiedSymbolHash {
    std::size_t operator()(const QualifiedSymbol& symbol) const noexcept {
        const auto first = std::hash<std::string>{}(symbol.module);
        const auto second = std::hash<std::string>{}(symbol.name);
        return first ^ (second << 1U);
    }
};

class SensitivityPolicy {
public:
    SensitivityPolicy(const PackageAnalysis& package,
                      const PTASensitivityConfiguration& configuration)
        : level_(configuration.level),
          maximum_path_variants_(configuration.maximum_path_variants) {
        if (level_ != PTASensitivityLevel::Insensitive &&
            level_ != PTASensitivityLevel::Selective &&
            level_ != PTASensitivityLevel::Complete)
            throw std::invalid_argument("PTA sensitivity level is invalid");
        if (maximum_path_variants_ == 0U)
            throw std::invalid_argument(
                "PTA maximum path variants must be nonzero");
        if (level_ != PTASensitivityLevel::Selective &&
            !configuration.functions.empty())
            throw std::invalid_argument(
                "PTA function selections require the selective level");
        constexpr auto valid_bits =
            static_cast<std::uint8_t>(kAllPTASensitivities);
        for (const auto& function : configuration.functions) {
            if (function.function == 0U)
                throw std::invalid_argument(
                    "PTA sensitivity function id zero is reserved");
            package.codeObject(function.function);
            const auto bits = static_cast<std::uint8_t>(function.attributes);
            if (bits == 0U || (bits & static_cast<std::uint8_t>(~valid_bits)) != 0U)
                throw std::invalid_argument(
                    "PTA sensitivity attributes are empty or invalid");
            if (!attributes_.emplace(
                    function.function, function.attributes).second)
                throw std::invalid_argument(
                    "PTA sensitivity function ids must be unique");
        }
    }

    PTASensitivity attributes(CodeObjectId function) const noexcept {
        if (level_ == PTASensitivityLevel::Complete)
            return kAllPTASensitivities;
        if (level_ == PTASensitivityLevel::Insensitive)
            return PTASensitivity::None;
        const auto found = attributes_.find(function);
        if (found == attributes_.end()) return PTASensitivity::None;
        auto result = found->second;
        if (hasSensitivity(result, PTASensitivity::Path))
            result = result | PTASensitivity::Flow;
        return result;
    }

    std::size_t maximumPathVariants() const noexcept {
        return maximum_path_variants_;
    }

private:
    PTASensitivityLevel level_;
    std::size_t maximum_path_variants_;
    std::unordered_map<CodeObjectId, PTASensitivity> attributes_;
};

bool reachesBlock(const cfg::ControlFlowGraph& graph, cfg::BlockId start,
                  cfg::BlockId target) {
    std::vector<cfg::BlockId> work{start};
    std::unordered_set<cfg::BlockId> visited;
    while (!work.empty()) {
        const auto current = work.back();
        work.pop_back();
        if (current == target) return true;
        if (!visited.insert(current).second) continue;
        for (const auto successor : graph.successors(current))
            work.push_back(successor);
    }
    return false;
}

bool blockIsCyclic(const cfg::ControlFlowGraph& graph, cfg::BlockId block) {
    for (const auto successor : graph.successors(block))
        if (reachesBlock(graph, successor, block)) return true;
    return false;
}

std::vector<std::vector<PTAPathDecision>> pathVariants(
    const cfg::ControlFlowGraph& graph, std::size_t maximum_variants) {
    std::vector<std::size_t> branches;
    std::vector<std::string> stable_local_predicates;
    const auto& program = graph.program();
    std::unordered_set<std::string> assigned_locals;
    for (const auto& instruction : program) {
        using O = bytecode::SemanticOpcode;
        if (instruction.opcode == O::StoreLocal ||
            instruction.opcode == O::DeleteLocal)
            assigned_locals.insert(instruction.symbol);
        else if (instruction.opcode == O::StoreLocalPair) {
            assigned_locals.insert(instruction.symbol);
            assigned_locals.insert(instruction.secondary_symbol);
        } else if (instruction.opcode == O::StoreLoadLocal)
            assigned_locals.insert(instruction.symbol);
    }
    for (const auto& block : graph.blocks()) {
        const auto instruction_index = block.instruction_indices.back();
        if (program[instruction_index].opcode ==
                bytecode::SemanticOpcode::ConditionalBranch &&
            !blockIsCyclic(graph, block.id)) {
            branches.push_back(instruction_index);
            std::string predicate;
            if (block.instruction_indices.size() >= 2U) {
                auto producer_position = block.instruction_indices.size() - 2U;
                while (producer_position != 0U &&
                       program[block.instruction_indices[producer_position]]
                           .provenance_preserving)
                    --producer_position;
                const auto& producer = program[
                    block.instruction_indices[producer_position]];
                if (producer.opcode == bytecode::SemanticOpcode::LoadLocal &&
                    producer.lexical_access ==
                        bytecode::LexicalAccessKind::FastLocal &&
                    assigned_locals.count(producer.symbol) == 0U)
                    predicate = producer.symbol;
            }
            stable_local_predicates.push_back(std::move(predicate));
        }
    }

    std::size_t selected = 0U;
    std::size_t variant_count = 1U;
    while (selected < branches.size() &&
           variant_count <= maximum_variants / 2U) {
        ++selected;
        variant_count *= 2U;
    }
    std::vector<std::vector<PTAPathDecision>> result;
    result.reserve(variant_count);
    for (std::size_t variant = 0; variant < variant_count; ++variant) {
        std::vector<PTAPathDecision> decisions;
        decisions.reserve(selected);
        std::unordered_map<std::string, bool> required_truth;
        bool feasible = true;
        for (std::size_t branch = 0; branch < selected; ++branch) {
            const bool take_jump =
                (variant & (std::size_t{1U} << branch)) != 0U;
            decisions.push_back({branches[branch], take_jump});
            const auto& predicate = stable_local_predicates[branch];
            if (predicate.empty()) continue;
            const bool truth = take_jump
                ? program[branches[branch]].jump_on_true
                : !program[branches[branch]].jump_on_true;
            const auto [position, inserted] =
                required_truth.emplace(predicate, truth);
            if (!inserted && position->second != truth) {
                feasible = false;
                break;
            }
        }
        if (feasible) result.push_back(std::move(decisions));
    }
    if (result.empty()) result.push_back({});
    return result;
}

std::vector<std::string> parameterNames(const CodeObjectAnalysis& code) {
    auto count = static_cast<std::size_t>(code.argument_count) +
                 static_cast<std::size_t>(code.keyword_only_argument_count);
    if (code.has_var_arguments) ++count;
    if (code.has_var_keywords) ++count;
    count = std::min(count, code.local_names.size());
    using Difference = std::vector<std::string>::difference_type;
    return {code.local_names.begin(),
            code.local_names.begin() + static_cast<Difference>(count)};
}

CodeObjectId createdCodeObject(const CodeObjectAnalysis& code,
                               std::size_t create_function_index) {
    const auto& program = code.cfg.program();
    while (create_function_index != 0U) {
        --create_function_index;
        const auto& candidate = program[create_function_index];
        if (candidate.opcode != bytecode::SemanticOpcode::LoadConst ||
            candidate.operand >= code.constant_code_objects.size())
            continue;
        const auto child = code.constant_code_objects[candidate.operand];
        if (child != 0U) return child;
    }
    return 0;
}

bool isClassBody(const CodeObjectAnalysis& code) {
    bool stores_module = false;
    bool stores_qualname = false;
    for (const auto& instruction : code.cfg.program()) {
        if (instruction.opcode != bytecode::SemanticOpcode::StoreGlobal) continue;
        stores_module |= instruction.symbol == "__module__";
        stores_qualname |= instruction.symbol == "__qualname__";
    }
    return stores_module && stores_qualname;
}

std::size_t classConstructionCall(const CodeObjectAnalysis& code,
                                  std::size_t create_function_index) {
    const auto& program = code.cfg.program();
    for (auto index = create_function_index + 1U; index < program.size(); ++index) {
        if (program[index].opcode == bytecode::SemanticOpcode::Call) return index;
        if (program[index].opcode == bytecode::SemanticOpcode::CreateFunction) break;
    }
    return program.size();
}

class Coordinator {
public:
    Coordinator(const PackageAnalysis& package,
                const PTASensitivityConfiguration& sensitivity,
                const ExternalCallConfiguration& external_calls)
        : package_(package), sensitivity_(package, sensitivity), builder_(pta_),
          requested_external_targets_(
              external_calls.qualified_targets.begin(),
              external_calls.qualified_targets.end()) {
        initializeModules();
    }

    PackageCallGraphResult build(
        const std::vector<CodeObjectId>& entry_points,
        PackageAnalysisObservations* observations) {
        collect_observations_ = observations != nullptr;
        for (const auto& code : package_.codeObjects()) {
            if (code.parent != 0U) continue;
            builder_.addCallSites(activate(code.id, 0U).call_sites);
        }
        finalizeStarImports();
        finalizeClassInheritance();
        std::unordered_set<CodeObjectId> rooted;
        for (const auto entry : entry_points) {
            const auto& code = package_.codeObject(entry);
            if (code.parent == 0U || !rooted.insert(entry).second) continue;
            external_entry_points_.insert(entry);
            const auto body = activate(entry, 0U);
            const auto function = function_ids_.find(entry);
            if (function == function_ids_.end())
                throw std::invalid_argument(
                    "entry point is not a registered module-level callable");
            builder_.addEntryPoint({function->second, entry}, body);
        }
        finalizeGlobalInputs();
        auto result = builder_.build(
            [this](cg::CallableTarget target, PTAContextId context) {
                return activate(target.code, context);
            },
            [this](const cg::OnTheFlyCallSite& site,
                   cg::CallableTarget target) {
                return contextFor(site, target);
            },
            [this](const cg::OnTheFlyCallSite& site,
                   cg::CallableTarget target) {
                return bindings(site, target);
            },
            [this](const std::vector<std::uint64_t>& unresolved_sites) {
                return materializeUnresolvedCallFallbacks(unresolved_sites);
            });
        if (observations != nullptr) collectObservations(*observations);
        std::unordered_set<CodeObjectId> analyzed_codes;
        for (const auto& instance : constraints_)
            analyzed_codes.insert(instance.code);
        const auto statistics = pta_.statistics();
        return {std::move(result.graph), analyzed_codes.size(),
                result.activated_callable_count, result.call_site_count,
                SoundnessCoverage::ConservativeTop,
                SoundnessCoverage::ConservativeTop,
                {statistics.value_count,
                 statistics.object_count,
                 statistics.constraint_count,
                 statistics.points_to_fact_count,
                 statistics.content_fact_count,
                 statistics.field_fact_count,
                 statistics.solver_iteration_count}};
    }

private:
    using ValuesBySymbol = std::unordered_map<
        QualifiedSymbol, std::vector<NodeId>, QualifiedSymbolHash>;

    struct ActivationKey {
        CodeObjectId code{};
        PTAContextId context{};
        bool operator==(const ActivationKey& other) const noexcept {
            return code == other.code && context == other.context;
        }
    };

    struct ActivationKeyHash {
        std::size_t operator()(const ActivationKey& key) const noexcept {
            const auto code = std::hash<CodeObjectId>{}(key.code);
            const auto context = std::hash<PTAContextId>{}(key.context);
            return code ^ (context + (code << 6U) + (code >> 2U));
        }
    };

    struct SiteTargetKey {
        std::uint64_t site{};
        CodeObjectId target{};
        bool operator==(const SiteTargetKey& other) const noexcept {
            return site == other.site && target == other.target;
        }
    };

    struct SiteTargetKeyHash {
        std::size_t operator()(const SiteTargetKey& key) const noexcept {
            const auto site = std::hash<std::uint64_t>{}(key.site);
            const auto target = std::hash<CodeObjectId>{}(key.target);
            return site ^ (target + (site << 6U) + (site >> 2U));
        }
    };

    struct StaticCallSite {
        CodeObjectId caller{};
        std::size_t instruction_index{};
        bool operator==(const StaticCallSite& other) const noexcept {
            return caller == other.caller &&
                   instruction_index == other.instruction_index;
        }
    };

    using CallString = std::array<StaticCallSite, kPTACallStringDepth>;

    struct CallContextKey {
        CodeObjectId callee{};
        CallString call_string{};
        bool operator==(const CallContextKey& other) const noexcept {
            return callee == other.callee &&
                   call_string == other.call_string;
        }
    };

    struct CallContextKeyHash {
        std::size_t operator()(const CallContextKey& key) const noexcept {
            auto result = std::hash<CodeObjectId>{}(key.callee);
            for (const auto& site : key.call_string) {
                const auto caller = std::hash<CodeObjectId>{}(site.caller);
                result ^= caller + (result << 6U) + (result >> 2U);
                const auto instruction =
                    std::hash<std::size_t>{}(site.instruction_index);
                result ^= instruction + (result << 6U) + (result >> 2U);
            }
            return result;
        }
    };

    struct ConstraintInstance {
        CodeObjectId code{};
        PTAContextId context{};
        PTAConstraintResult generated;
    };

    python::ReceiverProtocolFacts receiverProtocolFacts(
        ObjectId receiver, bytecode::PythonMethodSet candidates,
        const std::vector<AndersenPointerAnalysis::FieldFactView>& facts) {
        python::ReceiverProtocolFacts result;
        if (receiver == pta_.unknownObject()) {
            result.unknown = true;
            return result;
        }
        for (std::uint16_t method_index = 0U;
             method_index < static_cast<std::uint16_t>(
                 bytecode::PythonSpecialMethod::Count);
             ++method_index) {
            const auto method = static_cast<bytecode::PythonSpecialMethod>(
                method_index);
            if (!bytecode::containsPythonMethod(candidates, method)) continue;
            const auto field = pta_.internField(
                bytecode::specialMethodName(method));
            bool available = false;
            bool non_deferred = true;
            for (const auto& fact : facts) {
                if (fact.base != receiver || fact.field != field) continue;
                available = true;
                const auto behavior =
                    method_may_return_not_implemented_.find(fact.object);
                if (behavior == method_may_return_not_implemented_.end() ||
                    behavior->second)
                    non_deferred = false;
            }
            if (!available) continue;
            result.available_methods |= bytecode::pythonMethod(method);
            if (non_deferred)
                result.non_deferred_methods |= bytecode::pythonMethod(method);
        }
        if (!result.available_methods &&
            modeled_receiver_objects_.count(receiver) == 0U)
            result.unknown = true;
        return result;
    }

    bool objectHasField(
        ObjectId object, FieldId field,
        const std::vector<AndersenPointerAnalysis::FieldFactView>& facts) const {
        return std::any_of(
            facts.begin(), facts.end(), [&](const auto& fact) {
                return fact.base == object && fact.field == field;
            });
    }

    bool getattributeMayMissKnownMember(
        ObjectId receiver,
        const std::vector<AndersenPointerAnalysis::FieldFactView>& facts) {
        const auto field = pta_.internField(kGetAttributeSpecialMethodName);
        for (const auto& fact : facts) {
            if (fact.base != receiver || fact.field != field) continue;
            const auto behavior = method_may_miss_known_attribute_.find(
                fact.object);
            if (behavior == method_may_miss_known_attribute_.end() ||
                behavior->second)
                return true;
        }
        return false;
    }

    void addSelectedProtocolLoads(
        const std::vector<NodeId>& receivers,
        bytecode::PythonMethodSet methods, NodeId callee) {
        for (std::uint16_t method_index = 0U;
             method_index < static_cast<std::uint16_t>(
                 bytecode::PythonSpecialMethod::Count);
             ++method_index) {
            const auto method = static_cast<bytecode::PythonSpecialMethod>(
                method_index);
            if (!bytecode::containsPythonMethod(methods, method)) continue;
            const auto field = pta_.internField(
                bytecode::specialMethodName(method));
            for (const auto receiver : receivers)
                pta_.addFieldLoad(receiver, field, callee);
        }
    }

    bool protocolMethodIsConcrete(
        const std::vector<NodeId>& receiver_values,
        bytecode::PythonSpecialMethod method,
        const std::vector<AndersenPointerAnalysis::FieldFactView>& facts) {
        if (receiver_values.empty()) return false;
        const auto field = pta_.internField(
            bytecode::specialMethodName(method));
        bool saw_object = false;
        for (const auto value : receiver_values) {
            for (const auto object : pta_.pointsTo(value)) {
                saw_object = true;
                if (object == pta_.unknownObject()) return false;
                bool saw_callable = false;
                for (const auto& fact : facts) {
                    if (fact.base != object || fact.field != field) continue;
                    if (method_may_return_not_implemented_.count(
                            fact.object) == 0U)
                        return false;
                    saw_callable = true;
                }
                if (!saw_callable) return false;
            }
        }
        return saw_object;
    }

    bool materializePythonProtocolDispatch(
        ConstraintInstance& instance, std::size_t call_index,
        const std::vector<AndersenPointerAnalysis::FieldFactView>& facts) {
        auto& generated = instance.generated;
        const auto instruction_index =
            generated.call_instruction_indices[call_index];
        auto& group = generated.call_sites[call_index].unresolved_group;
        if (group.kind != cg::UnresolvedCalleeKind::PythonProtocol)
            return false;
        const auto site = generated.call_sites[call_index].id;
        auto original = original_protocol_methods_.find(site);
        if (original == original_protocol_methods_.end()) {
            if (!group.candidate_methods) return false;
            original = original_protocol_methods_.emplace(
                site, group.candidate_methods).first;
        }
        const auto candidates = original->second;
        const auto receiver_set = std::find_if(
            generated.protocol_receivers.begin(),
            generated.protocol_receivers.end(),
            [instruction_index](const auto& receivers) {
                return receivers.instruction_index == instruction_index;
            });
        if (receiver_set == generated.protocol_receivers.end()) return false;
        const auto& primary_values = receiver_set->primary;
        const auto& secondary_values = receiver_set->secondary;
        if (primary_values.empty()) return false;

        std::vector<ObjectId> primary_objects;
        for (const auto value : primary_values)
            for (const auto object : pta_.pointsTo(value))
                if (std::find(primary_objects.begin(), primary_objects.end(),
                              object) == primary_objects.end())
                    primary_objects.push_back(object);
        if (primary_objects.empty()) return false;

        std::vector<ObjectId> secondary_objects;
        for (const auto value : secondary_values)
            for (const auto object : pta_.pointsTo(value))
                if (std::find(secondary_objects.begin(), secondary_objects.end(),
                              object) == secondary_objects.end())
                    secondary_objects.push_back(object);
        if (!secondary_values.empty() && secondary_objects.empty())
            return false;
        if (secondary_objects.empty())
            secondary_objects.push_back(pta_.unknownObject());

        const auto& instruction = package_.codeObject(instance.code)
                                      .cfg.program()[instruction_index];
        python::ProtocolMethodSelection selected;
        if (instruction.opcode == bytecode::SemanticOpcode::LoadAttribute &&
            !instruction.symbol.empty()) {
            const auto member_field = pta_.internField(instruction.symbol);
            for (const auto receiver : primary_objects) {
                const auto receiver_facts = receiverProtocolFacts(
                    receiver, candidates, facts);
                const auto selection =
                    python::selectAttributeLoadMethods(
                        candidates, receiver_facts,
                        objectHasField(receiver, member_field, facts),
                        getattributeMayMissKnownMember(receiver, facts));
                selected.primary_methods |= selection.primary_methods;
            }
        } else if (instruction.opcode ==
                       bytecode::SemanticOpcode::StoreAttribute &&
                   !instruction.symbol.empty()) {
            const auto member_field = pta_.internField(instruction.symbol);
            for (const auto receiver : primary_objects) {
                const auto receiver_facts = receiverProtocolFacts(
                    receiver, candidates, facts);
                const auto selection =
                    python::selectAttributeStoreMethods(
                        candidates, receiver_facts,
                        objectHasField(receiver, member_field, facts));
                selected.primary_methods |= selection.primary_methods;
            }
        } else {
            for (const auto primary_object : primary_objects) {
                const auto primary = receiverProtocolFacts(
                    primary_object, candidates, facts);
                for (const auto secondary_object : secondary_objects) {
                    const auto secondary = receiverProtocolFacts(
                        secondary_object, candidates, facts);
                    const auto selection = python::selectProtocolMethods(
                        group.operation, candidates,
                        primary, secondary);
                    selected.primary_methods |= selection.primary_methods;
                    selected.secondary_methods |= selection.secondary_methods;
                }
            }
        }
        auto unresolved_methods = selected.allMethods();
        for (std::uint16_t method_index = 0U;
             method_index < static_cast<std::uint16_t>(
                 bytecode::PythonSpecialMethod::Count);
             ++method_index) {
            const auto method = static_cast<bytecode::PythonSpecialMethod>(
                method_index);
            const auto selected_on_primary = bytecode::containsPythonMethod(
                selected.primary_methods, method);
            const auto selected_on_secondary = bytecode::containsPythonMethod(
                selected.secondary_methods, method);
            if ((!selected_on_primary || protocolMethodIsConcrete(
                     primary_values, method, facts)) &&
                (!selected_on_secondary || protocolMethodIsConcrete(
                     secondary_values, method, facts)))
                unresolved_methods = bytecode::withoutPythonMethod(
                    unresolved_methods, method);
        }
        const auto previous_methods = group.candidate_methods;
        const auto previous_selection = materialized_protocol_methods_.find(
            generated.call_sites[call_index].id);
        const bool selection_changed =
            previous_selection == materialized_protocol_methods_.end() ||
            previous_selection->second.primary_methods !=
                selected.primary_methods ||
            previous_selection->second.secondary_methods !=
                selected.secondary_methods;
        group.candidate_methods = unresolved_methods;
        if (!selection_changed) {
            if (previous_methods == group.candidate_methods) return false;
            builder_.updateUnresolvedGroup(
                generated.call_sites[call_index].id, group);
            return true;
        }
        materialized_protocol_methods_.insert_or_assign(
            generated.call_sites[call_index].id, selected);
        builder_.updateUnresolvedGroup(
            generated.call_sites[call_index].id, group);
        addSelectedProtocolLoads(
            primary_values, selected.primary_methods,
            generated.call_sites[call_index].callee_value);
        addSelectedProtocolLoads(
            secondary_values, selected.secondary_methods,
            generated.call_sites[call_index].callee_value);
        return true;
    }

    bool materializeUnresolvedCallFallbacks(
        const std::vector<std::uint64_t>& unresolved_sites) {
        const std::unordered_set<std::uint64_t> unresolved(
            unresolved_sites.begin(), unresolved_sites.end());
        bool changed = materializeExternalModuleAttributes();
        const auto field_facts = pta_.fieldFacts();

        for (auto& instance : constraints_) {
            auto& generated = instance.generated;
            for (std::size_t index = 0; index < generated.call_sites.size(); ++index) {
                const auto site = generated.call_sites[index].id;
                if (unresolved.count(site) == 0U) continue;
                auto& group = generated.call_sites[index].unresolved_group;
                auto selected_external = group.external_callee;
                for (const auto object : pta_.pointsTo(
                         generated.call_sites[index].callee_value)) {
                    const auto external =
                        external_callee_by_callable_object_.find(object);
                    if (external ==
                        external_callee_by_callable_object_.end())
                        continue;
                    if (selected_external ==
                        bytecode::PythonExternalCallee::None)
                        selected_external = external->second;
                    else if (selected_external != external->second)
                        selected_external =
                            bytecode::PythonExternalCallee::None;
                }
                if (selected_external != group.external_callee) {
                    group.external_callee = selected_external;
                    builder_.updateUnresolvedGroup(site, group);
                    changed = true;
                }
                if (materializePythonProtocolDispatch(
                        instance, index, field_facts))
                    changed = true;
                if (materializePropertyDispatch(
                        instance, index, field_facts))
                    changed = true;
                if (isPropertySetterCall(instance, index) &&
                    deferred_property_setter_sites_.insert(site).second) {
                    changed = true;
                    continue;
                }
                if (!materialized_call_fallback_sites_.insert(site).second)
                    continue;
                const auto instruction = generated.call_instruction_indices[index];
                if (instruction >= generated.allocated_objects.size())
                    throw std::logic_error(
                        "unresolved PTA call-site instruction is out of range");
                const auto allocation = generated.allocated_objects[instruction];
                const auto& semantic_instruction =
                    package_.codeObject(instance.code).cfg.program()[instruction];
                bool modeled_result =
                    python::hasIntrinsicProtocolResult(
                        semantic_instruction.opcode);
                bool has_unmodeled_callee = false;
                const auto& callee_objects = pta_.pointsTo(
                    generated.call_sites[index].callee_value);
                if (callee_objects.empty()) has_unmodeled_callee = true;
                for (const auto object : callee_objects) {
                    const auto class_body = class_body_by_object_.find(object);
                    if (class_body != class_body_by_object_.end()) {
                        const auto model = class_models_.find(class_body->second);
                        if (model == class_models_.end())
                            throw std::logic_error(
                                "class object has no registered class model");
                        if (allocation == 0U)
                            throw std::logic_error(
                                "class call has no result allocation");
                        ensureAllocatedClassInstance(model->second, allocation);
                        pta_.addAddressOf(generated.call_results[index], allocation);
                        extra_object_origins_.push_back(
                            {allocation, PTAObjectOriginKind::ClassInstance,
                             class_body->second, 0U, instance.context});
                        modeled_result = true;
                        continue;
                    }
                    const auto super_owner =
                        super_owner_by_callable_object_.find(object);
                    if (super_owner != super_owner_by_callable_object_.end()) {
                        if (allocation == 0U)
                            throw std::logic_error(
                                "super call has no result allocation");
                        materializeSuperProxy(super_owner->second, allocation);
                        pta_.addAddressOf(
                            generated.call_results[index], allocation);
                        modeled_result = true;
                        continue;
                    }
                    if (identity_decorator_objects_.count(object) != 0U) {
                        for (const auto& argument :
                             generated.call_arguments[index])
                            for (const auto value : argument)
                                pta_.addCopy(
                                    value, generated.call_results[index]);
                        modeled_result = true;
                        continue;
                    }
                    if (property_constructor_objects_.count(object) != 0U) {
                        if (allocation == 0U)
                            throw std::logic_error(
                                "property call has no result allocation");
                        materializePropertyConstructor(
                            generated, index, allocation);
                        pta_.addAddressOf(
                            generated.call_results[index], allocation);
                        modeled_result = true;
                        continue;
                    }
                    const auto property_setter =
                        property_by_setter_callable_.find(object);
                    if (property_setter !=
                        property_by_setter_callable_.end()) {
                        materializePropertySetter(
                            generated, index, property_setter->second);
                        pta_.addAddressOf(
                            generated.call_results[index],
                            property_setter->second);
                        modeled_result = true;
                        continue;
                    }
                    const auto builtin_method =
                        builtin_method_by_callable_object_.find(object);
                    if (builtin_method !=
                        builtin_method_by_callable_object_.end()) {
                        generated.call_sites[index].unresolved_group = {
                            cg::UnresolvedCalleeKind::PythonProtocol,
                            bytecode::PythonProtocolOperation::DynamicProtocol,
                            builtin_method->second,
                            generated.call_sites[index].unresolved_group
                                .external_callee,
                        };
                        builder_.updateUnresolvedGroup(
                            generated.call_sites[index].id,
                            generated.call_sites[index].unresolved_group);
                        for (std::uint16_t method_index = 0U;
                             method_index < static_cast<std::uint16_t>(
                                 bytecode::PythonSpecialMethod::Count);
                             ++method_index) {
                            const auto method = static_cast<
                                bytecode::PythonSpecialMethod>(method_index);
                            if (!bytecode::containsPythonMethod(
                                    builtin_method->second, method))
                                continue;
                            for (const auto& argument :
                                 generated.call_arguments[index])
                                for (const auto value : argument)
                                    pta_.addFieldLoad(
                                        value,
                                        pta_.internField(
                                            bytecode::specialMethodName(method)),
                                        generated.call_sites[index].callee_value);
                        }
                        has_unmodeled_callee = true;
                        continue;
                    }
                    has_unmodeled_callee = true;
                }
                if (!modeled_result && allocation != 0U)
                    pta_.addAddressOf(generated.call_results[index], allocation);
                if (has_unmodeled_callee && !modeled_result)
                    pta_.addUnknown(generated.call_results[index]);
                changed = true;
            }
        }
        if (changed) pta_.solve();
        return changed;
    }

    bool materializeExternalModuleAttributes() {
        bool changed = false;
        for (const auto& instance : constraints_) {
            const auto& generated = instance.generated;
            const auto& program = package_.codeObject(instance.code)
                                      .cfg.program();
            for (std::size_t index = 0;
                 index < generated.attribute_load_bases.size(); ++index) {
                if (generated.attribute_load_bases[index].empty() ||
                    program[index].symbol.empty())
                    continue;
                const auto field = pta_.internField(program[index].symbol);
                for (const auto base :
                     generated.attribute_load_bases[index])
                    for (const auto object : pta_.pointsTo(base)) {
                        const auto module =
                            module_name_by_object_.find(object);
                        if (module == module_name_by_object_.end()) continue;
                        if (analyzed_module_names_.count(module->second) != 0U)
                            continue;
                        const auto external = python::moduleAttributeCallee(
                            module->second, program[index].symbol);
                        const auto qualified_target =
                            module->second + "." + program[index].symbol;
                        if (external ==
                                bytecode::PythonExternalCallee::None &&
                            requested_external_targets_.count(
                                qualified_target) == 0U)
                            continue;
                        const auto key = std::make_pair(object, field);
                        auto callable =
                            external_callable_objects_.find(key);
                        if (callable == external_callable_objects_.end()) {
                            const auto callable_object = pta_.newObject();
                            callable = external_callable_objects_
                                           .emplace(key, callable_object)
                                           .first;
                            if (external !=
                                bytecode::PythonExternalCallee::None)
                                external_callee_by_callable_object_.emplace(
                                    callable_object, external);
                            qualified_target_by_callable_object_.emplace(
                                callable_object, qualified_target);
                            pta_.addFieldAddress(
                                object, field, callable_object);
                            changed = true;
                        }
                    }
            }
        }
        if (changed) pta_.solve();
        return changed;
    }

    void collectObservations(PackageAnalysisObservations& observations) const {
        observations = {};
        std::unordered_set<FieldId> observed_attribute_fields;
        for (const auto& instance : constraints_) {
            const auto code_id = instance.code;
            const auto context = instance.context;
            const auto& generated = instance.generated;
            for (std::size_t index = 0; index < generated.value_nodes.size(); ++index) {
                const auto value = generated.value_nodes[index];
                const auto& objects = pta_.pointsTo(value);
                std::vector<ObjectId> sorted(objects.begin(), objects.end());
                std::sort(sorted.begin(), sorted.end());
                observations.points_to.push_back(
                    {code_id, index, value, std::move(sorted), context});
                if (index < generated.secondary_value_nodes.size() &&
                    generated.secondary_value_nodes[index] != 0U) {
                    const auto secondary =
                        generated.secondary_value_nodes[index];
                    const auto& secondary_objects = pta_.pointsTo(secondary);
                    std::vector<ObjectId> secondary_sorted(
                        secondary_objects.begin(), secondary_objects.end());
                    std::sort(secondary_sorted.begin(),
                              secondary_sorted.end());
                    observations.points_to.push_back(
                        {code_id, index, secondary,
                         std::move(secondary_sorted), context, 1U});
                }
                if (index < generated.allocated_objects.size() &&
                    generated.allocated_objects[index] != 0U)
                    observations.object_origins.push_back(
                        {generated.allocated_objects[index],
                         PTAObjectOriginKind::Instruction, code_id, index,
                         context});
            }
            for (std::size_t index = 0;
                 index < generated.attribute_load_bases.size(); ++index) {
                std::vector<ObjectId> objects;
                const auto append_objects = [&](const auto& bases) {
                    for (const auto base : bases)
                        for (const auto object : pta_.pointsTo(base))
                            if (std::find(objects.begin(), objects.end(), object) ==
                                objects.end())
                                objects.push_back(object);
                };
                append_objects(generated.attribute_load_bases[index]);
                append_objects(generated.attribute_store_bases[index]);
                if (objects.empty()) continue;
                std::sort(objects.begin(), objects.end());
                observations.heap_bases.push_back(
                    {code_id, index, context, std::move(objects)});
            }
            const auto& program = package_.codeObject(code_id).cfg.program();
            for (std::size_t index = 0; index < program.size(); ++index) {
                const auto field = pta_.findField(program[index].symbol);
                if (field != 0U &&
                    observed_attribute_fields.insert(field).second)
                    observations.attribute_field_names.push_back(
                        {field, std::string(pta_.debugField(field))});
                if (!generated.attribute_load_bases[index].empty())
                    observations.attribute_accesses.push_back({
                        code_id, index, context,
                        PTAAttributeAccessKind::Load, field,
                        generated.attribute_load_bases[index],
                        {generated.value_nodes[index]},
                    });
                if (!generated.attribute_store_bases[index].empty())
                    observations.attribute_accesses.push_back({
                        code_id, index, context,
                        PTAAttributeAccessKind::Store, field,
                        generated.attribute_store_bases[index],
                        generated.attribute_store_values[index],
                    });
                if (!generated.element_load_bases[index].empty())
                    observations.element_loads.push_back({
                        code_id, index, context,
                        generated.element_load_bases[index],
                        generated.element_load_keys[index],
                        {generated.value_nodes[index]},
                    });
            }
            for (std::size_t index = 0; index < generated.formal_parameters.size(); ++index)
                for (const auto object : pta_.pointsTo(generated.formal_parameters[index]))
                    observations.object_origins.push_back(
                        {object, PTAObjectOriginKind::Parameter, code_id, index,
                         context});
            for (std::size_t index = 0; index < generated.call_sites.size(); ++index) {
                observations.call_sites.push_back({
                    generated.call_sites[index].id,
                    code_id,
                    generated.call_instruction_indices[index],
                    context,
                    generated.call_results[index],
                    generated.call_arguments[index],
                    generated.call_explicit_argument_offsets[index],
                    generated.call_sites[index].callee_value,
                    generated.call_callee_inputs[index],
                });
                const auto& call_site = generated.call_sites[index];
                const auto typed_target =
                    python::externalCalleeQualifiedTarget(
                        call_site.unresolved_group.external_callee);
                if (!typed_target.empty())
                    observations.external_calls.push_back({
                        call_site.id, code_id,
                        generated.call_instruction_indices[index], context,
                        std::string(typed_target),
                    });
                for (const auto object :
                     pta_.pointsTo(call_site.callee_value)) {
                    const auto target =
                        qualified_target_by_callable_object_.find(object);
                    if (target !=
                        qualified_target_by_callable_object_.end())
                        observations.external_calls.push_back({
                            call_site.id, code_id,
                            generated.call_instruction_indices[index],
                            context, target->second,
                        });
                }
            }
            for (const auto& request : generated.import_requests)
                observations.import_requests.push_back({
                    code_id, request.instruction_index, context,
                    request.module, request.from_names,
                    request.relative_name_valid, request.lexical_module,
                    request.relative_level,
                });
        }
        observations.object_origins.insert(observations.object_origins.end(),
                                           extra_object_origins_.begin(),
                                           extra_object_origins_.end());
        for (const auto& [object, name] : module_name_by_object_)
            observations.module_objects.push_back({object, name});
        for (const auto& fact : pta_.contentFacts())
            observations.contents.push_back({fact.base, fact.object});
        std::unordered_set<FieldId> observed_fields;
        for (const auto& fact : pta_.fieldFacts()) {
            observations.fields.push_back({fact.base, fact.field, fact.object});
            if (observed_fields.insert(fact.field).second)
                observations.field_names.push_back(
                    {fact.field, std::string(pta_.debugField(fact.field))});
        }
        std::sort(observations.field_names.begin(), observations.field_names.end(),
                  [](const auto& first, const auto& second) {
                      return first.field < second.field;
                  });
        std::sort(observations.attribute_field_names.begin(),
                  observations.attribute_field_names.end(),
                  [](const auto& first, const auto& second) {
                      return first.field < second.field;
                  });
        std::sort(observations.module_objects.begin(),
                  observations.module_objects.end(),
                  [](const auto& first, const auto& second) {
                      return std::tie(first.object, first.name) <
                          std::tie(second.object, second.name);
                  });
        const auto value_order = [](const PTAValueObservation& first,
                                    const PTAValueObservation& second) {
            return std::tie(first.code, first.context,
                            first.instruction_index, first.output_index) <
                   std::tie(second.code, second.context,
                            second.instruction_index, second.output_index);
        };
        const auto origin_order = [](const PTAObjectOrigin& first,
                                     const PTAObjectOrigin& second) {
            return std::tie(first.object, first.kind, first.code, first.context,
                            first.index) <
                   std::tie(second.object, second.kind, second.code,
                            second.context, second.index);
        };
        const auto site_order = [](const PackageCallSiteObservation& first,
                                   const PackageCallSiteObservation& second) {
            return std::tie(first.code, first.context, first.instruction_index,
                            first.site) <
                   std::tie(second.code, second.context,
                            second.instruction_index, second.site);
        };
        std::sort(observations.points_to.begin(), observations.points_to.end(), value_order);
        std::sort(observations.object_origins.begin(), observations.object_origins.end(),
                  origin_order);
        observations.object_origins.erase(
            std::unique(observations.object_origins.begin(),
                        observations.object_origins.end(),
                        [](const PTAObjectOrigin& first, const PTAObjectOrigin& second) {
                            return first.object == second.object && first.kind == second.kind &&
                                   first.code == second.code && first.context == second.context &&
                                   first.index == second.index;
                        }),
            observations.object_origins.end());
        std::sort(observations.call_sites.begin(), observations.call_sites.end(), site_order);
        std::sort(
            observations.external_calls.begin(),
            observations.external_calls.end(),
            [](const auto& first, const auto& second) {
                return std::tie(
                           first.code, first.context,
                           first.instruction_index, first.site,
                           first.qualified_target) <
                       std::tie(
                           second.code, second.context,
                           second.instruction_index, second.site,
                           second.qualified_target);
            });
        observations.external_calls.erase(
            std::unique(
                observations.external_calls.begin(),
                observations.external_calls.end(),
                [](const auto& first, const auto& second) {
                    return first.site == second.site &&
                           first.code == second.code &&
                           first.instruction_index ==
                               second.instruction_index &&
                           first.context == second.context &&
                           first.qualified_target ==
                               second.qualified_target;
                }),
            observations.external_calls.end());
        observations.callable_objects = callable_objects_;
        std::sort(
            observations.callable_objects.begin(),
            observations.callable_objects.end(),
            [](const auto& first, const auto& second) {
                return std::tie(first.object, first.function, first.code,
                                first.defining_code, first.instruction_index,
                                first.defining_context) <
                       std::tie(second.object, second.function, second.code,
                                second.defining_code, second.instruction_index,
                                second.defining_context);
            });
        observations.callable_objects.erase(
            std::unique(
                observations.callable_objects.begin(),
                observations.callable_objects.end(),
                [](const auto& first, const auto& second) {
                    return first.object == second.object &&
                           first.function == second.function &&
                           first.code == second.code &&
                           first.defining_code == second.defining_code &&
                           first.instruction_index == second.instruction_index &&
                           first.defining_context == second.defining_context;
                }),
            observations.callable_objects.end());
        observations.call_activations = call_activations_;
        std::sort(observations.call_activations.begin(),
                  observations.call_activations.end(),
                  [](const auto& first, const auto& second) {
                      return std::tie(
                                 first.site, first.caller,
                                 first.instruction_index,
                                 first.caller_context, first.target,
                                 first.context) <
                             std::tie(
                                 second.site, second.caller,
                                 second.instruction_index,
                                 second.caller_context, second.target,
                                 second.context);
                  });
        observations.call_activations.erase(
            std::unique(
                observations.call_activations.begin(),
                observations.call_activations.end(),
                [](const auto& first, const auto& second) {
                    return first.site == second.site &&
                           first.caller == second.caller &&
                           first.instruction_index ==
                               second.instruction_index &&
                           first.caller_context == second.caller_context &&
                           first.target == second.target &&
                           first.context == second.context &&
                           first.bindings == second.bindings;
                }),
            observations.call_activations.end());
        observations.eager_code_executions = eager_code_executions_;
        std::sort(
            observations.eager_code_executions.begin(),
            observations.eager_code_executions.end(),
            [](const auto& first, const auto& second) {
                return std::tie(first.source, first.source_context,
                                first.instruction_index, first.target,
                                first.target_context) <
                       std::tie(second.source, second.source_context,
                                second.instruction_index, second.target,
                                second.target_context);
            });
        observations.eager_code_executions.erase(
            std::unique(
                observations.eager_code_executions.begin(),
                observations.eager_code_executions.end(),
                [](const auto& first, const auto& second) {
                    return first.source == second.source &&
                           first.instruction_index ==
                               second.instruction_index &&
                           first.source_context == second.source_context &&
                           first.target == second.target &&
                           first.target_context == second.target_context;
                }),
            observations.eager_code_executions.end());
        std::sort(
            observations.import_requests.begin(),
            observations.import_requests.end(),
            [](const auto& first, const auto& second) {
                return std::tie(first.source, first.source_context,
                                first.instruction_index, first.module,
                                first.from_names, first.relative_name_valid,
                                first.lexical_module, first.relative_level) <
                       std::tie(second.source, second.source_context,
                                second.instruction_index, second.module,
                                second.from_names, second.relative_name_valid,
                                second.lexical_module, second.relative_level);
            });
        observations.import_requests.erase(
            std::unique(
                observations.import_requests.begin(),
                observations.import_requests.end(),
                [](const auto& first, const auto& second) {
                    return first.source == second.source &&
                           first.instruction_index == second.instruction_index &&
                           first.source_context == second.source_context &&
                           first.module == second.module &&
                           first.from_names == second.from_names &&
                           first.relative_name_valid == second.relative_name_valid &&
                           first.lexical_module == second.lexical_module &&
                           first.relative_level == second.relative_level;
                }),
            observations.import_requests.end());
    }

    struct ClassModel {
        std::vector<ObjectId> class_objects;
        NodeId class_binding{};
        ObjectId instance_object{};
        NodeId instance_binding{};
        std::vector<std::pair<FieldId, NodeId>> instance_definitions;
        std::vector<std::pair<FieldId, NodeId>> own_instance_definitions;
        std::vector<std::pair<ObjectId, NodeId>> allocated_instance_bindings;
        std::vector<CodeObjectId> base_class_bodies;
        cg::CallableTarget instance_call_target{};
        cg::CallableTarget own_constructor_target{};
        cg::CallableTarget resolved_constructor_target{};
        std::vector<NodeId> base_candidate_values;
        CodeObjectId metaclass_body{};
    };

    struct PropertyModel {
        std::vector<NodeId> getters;
        std::vector<NodeId> setters;
        ObjectId setter_callable{};
    };

    void materializePropertyConstructor(
        const PTAConstraintResult& generated, std::size_t call_index,
        ObjectId descriptor) {
        auto model = property_models_.find(descriptor);
        if (model == property_models_.end()) {
            const auto setter_callable = pta_.newObject();
            model = property_models_.emplace(
                descriptor,
                PropertyModel{{}, {}, setter_callable}).first;
            property_by_setter_callable_.insert_or_assign(
                setter_callable, descriptor);
            pta_.addFieldAddress(
                descriptor,
                pta_.internField(kPropertySetterMethodName),
                setter_callable);
        }
        if (!generated.call_arguments[call_index].empty())
            for (const auto getter :
                 generated.call_arguments[call_index].front())
                if (std::find(model->second.getters.begin(),
                              model->second.getters.end(), getter) ==
                    model->second.getters.end())
                    model->second.getters.push_back(getter);
    }

    void materializePropertySetter(
        const PTAConstraintResult& generated, std::size_t call_index,
        ObjectId descriptor) {
        auto& model = property_models_.at(descriptor);
        for (const auto& argument : generated.call_arguments[call_index])
            for (const auto setter : argument)
                if (std::find(model.setters.begin(), model.setters.end(),
                              setter) == model.setters.end())
                    model.setters.push_back(setter);
    }

    bool materializePropertyDispatch(const ConstraintInstance& instance,
                                     std::size_t call_index,
                                     const std::vector<
                                         AndersenPointerAnalysis::FieldFactView>&
                                         facts) {
        const auto& generated = instance.generated;
        const auto instruction_index =
            generated.call_instruction_indices[call_index];
        const auto& instruction = package_.codeObject(instance.code)
                                      .cfg.program()[instruction_index];
        const std::vector<NodeId>* bases = nullptr;
        const bool load = instruction.opcode ==
            bytecode::SemanticOpcode::LoadAttribute;
        if (load)
            bases = &generated.attribute_load_bases[instruction_index];
        else if (instruction.opcode ==
                 bytecode::SemanticOpcode::StoreAttribute)
            bases = &generated.attribute_store_bases[instruction_index];
        if (bases == nullptr || instruction.symbol.empty()) return false;

        const auto field = pta_.internField(instruction.symbol);
        bool changed = false;
        for (const auto base : *bases)
            for (const auto base_object : pta_.pointsTo(base))
                for (const auto& fact : facts) {
                    if (fact.base != base_object || fact.field != field)
                        continue;
                    const auto property = property_models_.find(fact.object);
                    if (property == property_models_.end()) continue;
                    const auto& targets = load
                        ? property->second.getters
                        : property->second.setters;
                    for (const auto target : targets) {
                        const auto key = std::make_pair(
                            generated.call_sites[call_index].id, target);
                        if (std::find(materialized_property_dispatches_.begin(),
                                      materialized_property_dispatches_.end(),
                                      key) !=
                            materialized_property_dispatches_.end())
                            continue;
                        materialized_property_dispatches_.push_back(key);
                        pta_.addCopy(
                            target,
                            generated.call_sites[call_index].callee_value);
                        changed = true;
                    }
                }
        return changed;
    }

    bool isPropertySetterCall(const ConstraintInstance& instance,
                              std::size_t call_index) const {
        const auto instruction_index =
            instance.generated.call_instruction_indices[call_index];
        const auto& program = package_.codeObject(instance.code).cfg.program();
        for (auto index = instruction_index; index != 0U;) {
            --index;
            const auto& instruction = program[index];
            if (instruction.opcode == bytecode::SemanticOpcode::LoadAttribute)
                return instruction.symbol == kPropertySetterMethodName;
            if (instruction.opcode == bytecode::SemanticOpcode::Call)
                return false;
        }
        return false;
    }

    void wireMetaclassConstructor(
        const ActivationKey& activation_key,
        const cg::CallableBodySummary& body,
        CodeObjectId constructed_class) {
        if (metaclass_constructor_dispatch_objects_.count(activation_key) != 0U)
            return;
        const auto constructor =
            constructor_targets_by_class_body_.find(constructed_class);
        if (constructor == constructor_targets_by_class_body_.end()) return;
        const auto& program = package_.codeObject(activation_key.code).cfg.program();
        for (const auto& site : body.call_sites) {
            if (program[site.instruction_index].opcode !=
                bytecode::SemanticOpcode::Call)
                continue;
            bool calls_runtime_constructor = false;
            for (auto index = site.instruction_index; index != 0U;) {
                --index;
                const auto& instruction = program[index];
                if (instruction.opcode ==
                        bytecode::SemanticOpcode::LoadAttribute) {
                    calls_runtime_constructor =
                        instruction.symbol == kCallSpecialMethodName;
                    break;
                }
                if (instruction.opcode == bytecode::SemanticOpcode::Call)
                    break;
            }
            if (!calls_runtime_constructor) continue;
            const auto dispatch_object = pta_.newObject();
            builder_.registerCallable(dispatch_object, constructor->second);
            pta_.addAddressOf(site.site.callee_value, dispatch_object);
            metaclass_constructor_dispatch_objects_.insert_or_assign(
                activation_key, dispatch_object);
            return;
        }
    }

    NodeId ensureAllocatedClassInstance(ClassModel& model,
                                        ObjectId instance_object) {
        modeled_receiver_objects_.insert(instance_object);
        if (instance_object == model.instance_object)
            return model.instance_binding;
        const auto existing = std::find_if(
            model.allocated_instance_bindings.begin(),
            model.allocated_instance_bindings.end(),
            [&](const auto& entry) {
                return entry.first == instance_object;
            });
        if (existing != model.allocated_instance_bindings.end())
            return existing->second;

        const auto binding = pta_.newValue();
        pta_.addAddressOf(binding, instance_object);
        for (const auto& [field, definition] : model.instance_definitions)
            pta_.addFieldStore(definition, binding, field);
        model.allocated_instance_bindings.emplace_back(
            instance_object, binding);
        if (model.instance_call_target.code != 0U)
            builder_.assignCallable(
                instance_object, model.instance_call_target);
        return binding;
    }

    void addInstanceDefinition(ClassModel& model, FieldId field,
                               NodeId definition) {
        model.instance_definitions.emplace_back(field, definition);
        model.own_instance_definitions.emplace_back(field, definition);
        pta_.addFieldStore(definition, model.instance_binding, field);
        for (const auto& entry : model.allocated_instance_bindings)
            pta_.addFieldStore(definition, entry.second, field);
    }

    void discoverBaseClasses(ClassModel& model,
                             const PTAConstraintResult& generated,
                             std::size_t call_instruction) {
        const auto call = std::find(
            generated.call_instruction_indices.begin(),
            generated.call_instruction_indices.end(), call_instruction);
        if (call == generated.call_instruction_indices.end()) return;
        const auto call_index = static_cast<std::size_t>(
            call - generated.call_instruction_indices.begin());
        pta_.solve();
        for (const auto& argument : generated.call_arguments[call_index])
            for (const auto value : argument) {
                model.base_candidate_values.push_back(value);
                for (const auto object : pta_.pointsTo(value)) {
                    const auto base = class_body_by_object_.find(object);
                    if (base == class_body_by_object_.end() ||
                        std::find(model.base_class_bodies.begin(),
                                  model.base_class_bodies.end(),
                                  base->second) !=
                            model.base_class_bodies.end())
                        continue;
                    model.base_class_bodies.push_back(base->second);
                }
            }
    }

    void discoverMetaclass(const CodeObjectAnalysis& code, ClassModel& model,
                           const PTAConstraintResult& generated,
                           std::size_t call_instruction) {
        if (call_instruction >= code.cfg.program().size())
            return;
        const auto& instruction = code.cfg.program()[call_instruction];
        const auto keyword = std::find(
            instruction.keyword_names.begin(),
            instruction.keyword_names.end(), kMetaclassKeywordName);
        if (!instruction.keyword_arguments ||
            keyword == instruction.keyword_names.end()) return;
        const auto call = std::find(
            generated.call_instruction_indices.begin(),
            generated.call_instruction_indices.end(), call_instruction);
        if (call == generated.call_instruction_indices.end()) return;
        const auto call_index = static_cast<std::size_t>(
            call - generated.call_instruction_indices.begin());
        const auto& arguments = generated.call_arguments[call_index];
        if (instruction.keyword_names.size() > arguments.size()) return;
        const auto keyword_offset = static_cast<std::size_t>(
            keyword - instruction.keyword_names.begin());
        const auto argument_index = arguments.size() -
                                    instruction.keyword_names.size() +
                                    keyword_offset;
        pta_.solve();
        for (const auto value : arguments[argument_index])
            for (const auto object : pta_.pointsTo(value)) {
                const auto metaclass = class_body_by_object_.find(object);
                if (metaclass != class_body_by_object_.end()) {
                    model.metaclass_body = metaclass->second;
                    model.base_class_bodies.erase(
                        std::remove(model.base_class_bodies.begin(),
                                    model.base_class_bodies.end(),
                                    model.metaclass_body),
                        model.base_class_bodies.end());
                    return;
                }
            }
    }

    void finalizeClassInheritance() {
        pta_.solve();
        for (auto& [class_body, model] : class_models_) {
            static_cast<void>(class_body);
            for (const auto value : model.base_candidate_values)
                for (const auto object : pta_.pointsTo(value)) {
                    const auto base = class_body_by_object_.find(object);
                    if (base == class_body_by_object_.end() ||
                        base->second == model.metaclass_body ||
                        std::find(model.base_class_bodies.begin(),
                                  model.base_class_bodies.end(),
                                  base->second) !=
                            model.base_class_bodies.end())
                        continue;
                    model.base_class_bodies.push_back(base->second);
                }
            model.base_candidate_values.clear();
            model.base_candidate_values.shrink_to_fit();
        }
        for (std::size_t pass = 0U; pass < class_models_.size(); ++pass)
            for (auto& entry : class_models_)
                inheritBaseDefinitions(entry.second);
        for (auto& [class_body, model] : class_models_) {
            if (model.resolved_constructor_target.code == 0U) continue;
            constructor_targets_by_class_body_.insert_or_assign(
                class_body, model.resolved_constructor_target);
            if (model.metaclass_body != 0U) continue;
            for (const auto class_object : model.class_objects)
                builder_.assignCallable(
                    class_object, model.resolved_constructor_target);
        }
    }

    void inheritBaseDefinitions(ClassModel& model) {
        std::unordered_set<FieldId> defined_fields;
        model.instance_definitions = model.own_instance_definitions;
        for (const auto& entry : model.own_instance_definitions)
            defined_fields.insert(entry.first);
        const auto inherit = [&](const ClassModel& base, bool own_only) {
            const auto& definitions = own_only
                ? base.own_instance_definitions
                : base.instance_definitions;
            for (const auto& [field, definition] : definitions) {
                if (!defined_fields.insert(field).second) continue;
                model.instance_definitions.emplace_back(field, definition);
                pta_.addFieldStore(definition, model.instance_binding, field);
                for (const auto& entry : model.allocated_instance_bindings)
                    pta_.addFieldStore(definition, entry.second, field);
                pta_.addFieldStore(definition, model.class_binding, field);
            }
        };
        // Direct definitions outrank definitions inherited by an earlier
        // base. This preserves Python's C3 result for the common diamond
        // shape without flattening an ancestor ahead of a sibling override.
        for (const auto base_body : model.base_class_bodies) {
            const auto base = class_models_.find(base_body);
            if (base != class_models_.end()) inherit(base->second, true);
        }
        for (const auto base_body : model.base_class_bodies) {
            const auto base = class_models_.find(base_body);
            if (base == class_models_.end()) continue;
            inherit(base->second, false);
        }
        model.resolved_constructor_target = model.own_constructor_target;
        if (model.resolved_constructor_target.code == 0U) {
            for (const auto base_body : model.base_class_bodies) {
                const auto base = class_models_.find(base_body);
                if (base == class_models_.end() ||
                    base->second.resolved_constructor_target.code == 0U)
                    continue;
                model.resolved_constructor_target =
                    base->second.resolved_constructor_target;
                break;
            }
        }
    }

    void materializeSuperProxy(CodeObjectId owner_body,
                               ObjectId proxy_object) {
        if (!materialized_super_proxies_.insert(proxy_object).second) return;
        const auto owner = class_models_.find(owner_body);
        if (owner == class_models_.end())
            throw std::logic_error("super proxy owner has no class model");
        const auto binding = pta_.newValue();
        pta_.addAddressOf(binding, proxy_object);
        std::unordered_set<FieldId> defined_fields;
        for (const auto base_body : owner->second.base_class_bodies) {
            const auto base = class_models_.find(base_body);
            if (base == class_models_.end()) continue;
            for (const auto& [field, definition] :
                 base->second.instance_definitions) {
                if (!defined_fields.insert(field).second) continue;
                pta_.addFieldStore(definition, binding, field);
            }
        }
    }

    cg::CallableBodySummary activate(CodeObjectId code_id,
                                     PTAContextId activation_context) {
        const ActivationKey key{code_id, activation_context};
        const auto existing = activations_.find(key);
        if (existing != activations_.end()) return existing->second;
        activations_.emplace(key, cg::CallableBodySummary{});

        const auto attributes = sensitivity_.attributes(code_id);
        const auto& code = package_.codeObject(code_id);
        const auto contextual_receiver = receiver_objects_by_activation_.find(key);
        const auto receiver = receiver_objects_by_code_.find(code_id);
        const auto receiver_object = contextual_receiver !=
                receiver_objects_by_activation_.end()
            ? contextual_receiver->second
            : (receiver == receiver_objects_by_code_.end()
                   ? ObjectId{0U} : receiver->second);
        const auto decisions = hasSensitivity(attributes, PTASensitivity::Path)
            ? pathVariants(code.cfg, sensitivity_.maximumPathVariants())
            : std::vector<std::vector<PTAPathDecision>>{{}};
        std::vector<cg::CallableBodySummary> variants;
        variants.reserve(decisions.size());
        for (std::size_t variant = 0; variant < decisions.size(); ++variant) {
            const auto context = hasSensitivity(attributes, PTASensitivity::Path) &&
                                         (activation_context == 0U ||
                                          decisions.size() > 1U)
                                     ? newContext()
                                     : activation_context;
            if (context != activation_context && activation_context != 0U) {
                const auto lineage = context_call_strings_.find(
                    activation_context);
                if (lineage != context_call_strings_.end())
                    context_call_strings_.emplace(context, lineage->second);
            }
            SemanticPTAOptions options;
            options.flow_sensitive =
                hasSensitivity(attributes, PTASensitivity::Flow);
            options.ordered_module_globals = code.parent == 0U;
            options.allocation_sensitive =
                hasSensitivity(attributes, PTASensitivity::Context);
            options.defer_global_unknowns = true;
            options.defer_call_fallbacks = true;
            options.seed_external_parameters =
                activation_context == 0U &&
                external_entry_points_.count(code_id) != 0U;
            options.path_decisions = decisions[variant];
            auto generated = SemanticPTAConstraintBuilder(
                pta_, &module_objects_, code.module_name, code.module_is_package,
                receiver_object,
                std::move(options))
                                 .build(code.cfg, code.id, parameterNames(code),
                                        code.free_names);
            constraints_.push_back(
                {code_id, context, std::move(generated)});
            auto& stored = constraints_.back().generated;
            connectNamespaces(code, stored);
            variants.push_back(summarize(stored, code, context));
            registerCreatedCallables(code, stored, context);
            // Name lookup is needed only while wiring closure construction.
            // The solved graph retains numeric nodes/fields, so release the
            // per-activation string map before keeping observations.
            stored.lexical_cell_bindings.clear();
            stored.lexical_cell_bindings.rehash(0U);
        }

        cg::CallableBodySummary aggregate;
        if (variants.size() == 1U) {
            aggregate = std::move(variants.front());
        } else {
            const auto formal_count = variants.front().formal_parameters.size();
            for (const auto& variant : variants)
                if (variant.formal_parameters.size() != formal_count)
                    throw std::logic_error(
                        "path-sensitive PTA variants disagree on formal parameters");
            aggregate.formal_parameters.reserve(formal_count);
            for (std::size_t formal = 0; formal < formal_count; ++formal) {
                const auto shared = pta_.newValue();
                aggregate.formal_parameters.push_back(shared);
                for (const auto& variant : variants)
                    pta_.addCopy(shared, variant.formal_parameters[formal]);
            }
            for (auto& variant : variants) {
                aggregate.return_values.insert(
                    aggregate.return_values.end(),
                    variant.return_values.begin(), variant.return_values.end());
                aggregate.call_sites.insert(
                    aggregate.call_sites.end(),
                    std::make_move_iterator(variant.call_sites.begin()),
                    std::make_move_iterator(variant.call_sites.end()));
            }
            if (std::any_of(
                    variants.begin(), variants.end(), [](const auto& variant) {
                        return variant.capture_environment != 0U;
                    })) {
                aggregate.capture_environment = pta_.newValue();
                for (const auto& variant : variants)
                    if (variant.capture_environment != 0U)
                        pta_.addCopy(aggregate.capture_environment,
                                     variant.capture_environment);
            }
        }
        if (aggregate.capture_environment != 0U &&
            activation_context == 0U &&
            external_entry_points_.count(code_id) != 0U)
            pta_.addUnknown(aggregate.capture_environment);
        activations_.at(key) = aggregate;
        return aggregate;
    }

    FieldId captureField(std::size_t slot) {
        return pta_.internField(
            std::string(kPTACaptureFieldPrefix) + std::to_string(slot));
    }

    cg::CallableBodySummary summarize(const PTAConstraintResult& generated,
                                      const CodeObjectAnalysis& code,
                                      PTAContextId context) {
        if (generated.call_sites.size() != generated.call_arguments.size() ||
            generated.call_sites.size() !=
                generated.call_explicit_argument_offsets.size() ||
            generated.call_sites.size() != generated.call_callee_inputs.size() ||
            generated.call_sites.size() != generated.call_results.size() ||
            generated.call_sites.size() != generated.call_instruction_indices.size())
            throw std::logic_error("PTA call-site boundary vectors are inconsistent");

        std::vector<cg::OnTheFlyCallSite> sites;
        sites.reserve(generated.call_sites.size());
        for (std::size_t index = 0; index < generated.call_sites.size(); ++index) {
            const auto instruction = generated.call_instruction_indices[index];
            if (instruction < generated.allocated_objects.size() &&
                generated.allocated_objects[instruction] != 0U)
                call_result_allocations_.insert_or_assign(
                    generated.call_results[index],
                    generated.allocated_objects[instruction]);
            sites.push_back({generated.call_sites[index], generated.call_arguments[index],
                             generated.call_results[index],
                             instruction,
                             context});
        }
        NodeId environment = 0U;
        if (!code.free_names.empty()) {
            environment = pta_.newValue();
            for (std::size_t slot = 0; slot < code.free_names.size(); ++slot) {
                const auto binding = generated.lexical_cell_bindings.find(
                    code.free_names[slot]);
                if (binding == generated.lexical_cell_bindings.end())
                    throw std::logic_error(
                        "free variable has no lexical cell binding");
                pta_.addFieldLoad(environment, captureField(slot),
                                  binding->second);
            }
        }
        return {generated.formal_parameters, generated.return_values,
                std::move(sites), environment};
    }

    PTAContextId newContext() {
        if (next_context_id_ == 0U)
            throw std::overflow_error("PTA context id space exhausted");
        return next_context_id_++;
    }

    PTAContextId contextFor(const cg::OnTheFlyCallSite& site,
                            cg::CallableTarget target) {
        PTAContextId context = 0U;
        if (hasSensitivity(sensitivity_.attributes(target.code),
                           PTASensitivity::Context)) {
            CallString call_string{};
            call_string.front() = {
                site.site.caller, site.instruction_index};
            const auto parent = context_call_strings_.find(
                site.caller_context);
            if (parent != context_call_strings_.end())
                for (std::size_t depth = 1;
                     depth < call_string.size(); ++depth)
                    call_string[depth] = parent->second[depth - 1U];
            const CallContextKey key{target.code, call_string};
            const auto found = call_contexts_.find(key);
            if (found != call_contexts_.end()) {
                context = found->second;
            } else {
                context = newContext();
                call_contexts_.emplace(key, context);
                context_call_strings_.emplace(context, call_string);
            }
        }
        const auto constructor = constructor_class_bodies_.find(target.code);
        const auto allocation = call_result_allocations_.find(site.call_result);
        if (context != 0U && constructor != constructor_class_bodies_.end() &&
            allocation != call_result_allocations_.end()) {
            const auto model = class_models_.find(constructor->second);
            if (model == class_models_.end())
                throw std::logic_error(
                    "constructor has no registered class model");
            ensureAllocatedClassInstance(model->second, allocation->second);
            receiver_objects_by_activation_.insert_or_assign(
                ActivationKey{target.code, context}, allocation->second);
            pta_.addAddressOf(site.call_result, allocation->second);
            extra_object_origins_.push_back(
                {allocation->second, PTAObjectOriginKind::ClassInstance,
                 constructor->second, 0U, context});
        }
        const auto metaclass_owner =
            metaclass_body_by_call_code_.find(target.code);
        if (metaclass_owner != metaclass_body_by_call_code_.end()) {
            for (const auto class_object :
                 pta_.pointsTo(site.site.callee_value)) {
                const auto class_body =
                    class_body_by_object_.find(class_object);
                if (class_body == class_body_by_object_.end()) continue;
                auto model = class_models_.find(class_body->second);
                if (model == class_models_.end() ||
                    model->second.metaclass_body != metaclass_owner->second)
                    continue;
                receiver_objects_by_activation_.insert_or_assign(
                    ActivationKey{target.code, context}, class_object);
                constructed_class_by_activation_.insert_or_assign(
                    ActivationKey{target.code, context}, class_body->second);
                if (allocation != call_result_allocations_.end()) {
                    ensureAllocatedClassInstance(
                        model->second, allocation->second);
                    pta_.addAddressOf(site.call_result, allocation->second);
                    extra_object_origins_.push_back(
                        {allocation->second,
                         PTAObjectOriginKind::ClassInstance,
                         class_body->second, 0U, context});
                }
                break;
            }
        }
        if (collect_observations_)
            call_activations_.push_back(
                {site.site.id, site.site.caller, site.instruction_index,
                 site.caller_context, target.code, context, {}});
        target_contexts_.insert_or_assign(
            SiteTargetKey{site.site.id, target.code}, context);
        return context;
    }

    void connectNamespaces(const CodeObjectAnalysis& code,
                           const PTAConstraintResult& generated) {
        const auto class_model = class_models_.find(code.id);
        const auto owner_model = class_models_.find(code.parent);
        if (owner_model != class_models_.end()) {
            const auto& program = code.cfg.program();
            for (std::size_t index = 0U; index < program.size(); ++index) {
                const auto& instruction = program[index];
                if (!instruction.super_attribute || instruction.symbol.empty())
                    continue;
                const auto field = pta_.internField(instruction.symbol);
                std::unordered_set<NodeId> definitions;
                for (const auto base_body : owner_model->second.base_class_bodies) {
                    const auto base = class_models_.find(base_body);
                    if (base == class_models_.end()) continue;
                    for (const auto& [candidate_field, definition] :
                         base->second.instance_definitions) {
                        if (candidate_field != field ||
                            !definitions.insert(definition).second)
                            continue;
                        pta_.addCopy(definition,
                                     generated.value_nodes[index]);
                    }
                }
            }
        }
        for (const auto& [name, values] : generated.global_definitions) {
            if (class_model != class_models_.end()) {
                const auto field = pta_.internField(name);
                for (const auto definition : values) {
                    addInstanceDefinition(
                        class_model->second, field, definition);
                    pta_.addFieldStore(definition,
                                       class_model->second.class_binding, field);
                }
                continue;
            }
            const QualifiedSymbol symbol{code.module_name, name};
            const auto inputs = global_inputs_.find(symbol);
            if (inputs != global_inputs_.end())
                for (const auto definition : values)
                    for (const auto input : inputs->second)
                        pta_.addCopy(definition, input);
            auto& definitions = global_definitions_[symbol];
            definitions.insert(definitions.end(), values.begin(), values.end());
            const auto module_binding = module_bindings_.find(code.module_name);
            if (module_binding != module_bindings_.end()) {
                const auto field = pta_.internField(name);
                for (const auto definition : values)
                    pta_.addFieldStore(definition, module_binding->second, field);
            }
        }
        for (const auto& [name, values] : generated.global_inputs) {
            const auto owner = class_models_.find(code.parent);
            if (name == kSuperBuiltinName && owner != class_models_.end()) {
                const auto callable_object = pta_.newObject();
                super_owner_by_callable_object_.insert_or_assign(
                    callable_object, code.parent);
                for (const auto input : values)
                    pta_.addAddressOf(input, callable_object);
                continue;
            }
            if (name == kClassMethodBuiltinName ||
                name == kStaticMethodBuiltinName) {
                const auto callable_object = pta_.newObject();
                identity_decorator_objects_.insert(callable_object);
                for (const auto input : values)
                    pta_.addAddressOf(input, callable_object);
                continue;
            }
            if (name == kPropertyBuiltinName) {
                const auto callable_object = pta_.newObject();
                property_constructor_objects_.insert(callable_object);
                for (const auto input : values)
                    pta_.addAddressOf(input, callable_object);
                continue;
            }
            const QualifiedSymbol symbol{code.module_name, name};
            const auto builtin_dispatch =
                python::builtinProtocolDispatch(name);
            if (builtin_dispatch.candidate_methods &&
                global_definitions_.find(symbol) == global_definitions_.end()) {
                const auto callable_object = pta_.newObject();
                builtin_method_by_callable_object_.insert_or_assign(
                    callable_object, builtin_dispatch.candidate_methods);
                for (const auto input : values)
                    pta_.addAddressOf(input, callable_object);
                continue;
            }
            const auto definitions = global_definitions_.find(symbol);
            if (definitions != global_definitions_.end())
                for (const auto definition : definitions->second)
                    for (const auto input : values) pta_.addCopy(definition, input);
            auto& inputs = global_inputs_[symbol];
            inputs.insert(inputs.end(), values.begin(), values.end());
            if (globals_finalized_ && definitions == global_definitions_.end())
                for (const auto input : values) pta_.addUnknown(input);
        }
        for (const auto source : generated.star_import_sources)
            pending_star_imports_.emplace_back(code.module_name, source);
    }

    void finalizeStarImports() {
        pta_.solve();
        std::vector<std::pair<std::string, std::string>> imports;
        for (const auto& [importer, source] : pending_star_imports_)
            for (const auto object : pta_.pointsTo(source)) {
                const auto imported = module_name_by_object_.find(object);
                if (imported == module_name_by_object_.end()) continue;
                const auto relation = std::make_pair(
                    imported->second, importer);
                if (std::find(imports.begin(), imports.end(), relation) ==
                    imports.end())
                    imports.push_back(relation);
            }

        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& [imported, importer] : imports) {
                std::vector<std::pair<std::string, std::vector<NodeId>>>
                    exported;
                for (const auto& [symbol, definitions] : global_definitions_)
                    if (symbol.module == imported && !symbol.name.empty() &&
                        symbol.name.front() != '_')
                        exported.emplace_back(symbol.name, definitions);
                for (const auto& [name, definitions] : exported) {
                    const QualifiedSymbol imported_symbol{importer, name};
                    auto& destination = global_definitions_[imported_symbol];
                    const auto inputs = global_inputs_.find(imported_symbol);
                    const auto module_binding = module_bindings_.find(importer);
                    const auto field = pta_.internField(name);
                    for (const auto definition : definitions) {
                        if (std::find(destination.begin(), destination.end(),
                                      definition) != destination.end())
                            continue;
                        destination.push_back(definition);
                        changed = true;
                        if (inputs != global_inputs_.end())
                            for (const auto input : inputs->second)
                                pta_.addCopy(definition, input);
                        if (module_binding != module_bindings_.end())
                            pta_.addFieldStore(
                                definition, module_binding->second, field);
                    }
                }
            }
        }
        pending_star_imports_.clear();
        pending_star_imports_.shrink_to_fit();
    }

    void finalizeGlobalInputs() {
        for (const auto& [symbol, inputs] : global_inputs_)
            if (global_definitions_.find(symbol) == global_definitions_.end())
                for (const auto input : inputs) pta_.addUnknown(input);
        globals_finalized_ = true;
    }

    void initializeModules() {
        for (const auto& code : package_.codeObjects()) {
            if (code.parent == 0U) {
                analyzed_module_names_.insert(code.module_name);
                ensureModule(code.module_name);
            }
            // Imported modules outside the analyzed package still need a
            // stable object identity. This lets later PTA refinement qualify
            // external attributes by their actual receiver module.
            for (const auto& instruction : code.cfg.program()) {
                if (instruction.opcode !=
                        bytecode::SemanticOpcode::ImportModule ||
                    (instruction.import_level &&
                     *instruction.import_level != 0U) ||
                    instruction.symbol.empty())
                    continue;
                if (instruction.import_fromlist) {
                    ensureModule(instruction.symbol);
                    continue;
                }
                const auto separator = instruction.symbol.find('.');
                ensureModule(
                    separator == std::string::npos
                        ? instruction.symbol
                        : instruction.symbol.substr(0U, separator));
            }
        }
        for (const auto& [name, object] : module_objects_by_name_) {
            auto separator = name.rfind('.');
            if (separator == std::string::npos) continue;
            const auto parent_name = name.substr(0, separator);
            const auto child_name = name.substr(separator + 1U);
            const auto parent = ensureModule(parent_name);
            pta_.addFieldAddress(parent, pta_.internField(child_name), object);
        }
    }

    ObjectId ensureModule(const std::string& name) {
        const auto found = module_objects_by_name_.find(name);
        if (found != module_objects_by_name_.end()) return found->second;
        const auto object = pta_.newObject();
        modeled_receiver_objects_.insert(object);
        const auto binding = pta_.newValue();
        pta_.addAddressOf(binding, object);
        module_objects_by_name_.emplace(name, object);
        module_name_by_object_.emplace(object, name);
        module_bindings_.emplace(name, binding);
        module_objects_.exact[name].push_back(object);
        const auto separator = name.rfind('.');
        const auto suffix = separator == std::string::npos
            ? name : name.substr(separator + 1U);
        module_objects_.suffix[suffix].push_back(object);
        extra_object_origins_.push_back(
            {object, PTAObjectOriginKind::Module, 0U, 0U});
        if (separator != std::string::npos) ensureModule(name.substr(0, separator));
        return object;
    }

    void registerCreatedCallables(const CodeObjectAnalysis& code,
                                  const PTAConstraintResult& generated,
                                  PTAContextId context) {
        const auto& program = code.cfg.program();
        for (std::size_t instruction_index = 0;
             instruction_index < program.size(); ++instruction_index) {
            if (program[instruction_index].opcode !=
                bytecode::SemanticOpcode::CreateFunction)
                continue;
            const auto child = createdCodeObject(code, instruction_index);
            const auto object = generated.allocated_objects[instruction_index];
            if (child == 0U || object == 0U) continue;
            extra_object_origins_.push_back(
                {object, PTAObjectOriginKind::Callable, child, 0U, context});

            const auto& child_code = package_.codeObject(child);
            for (std::size_t slot = 0; slot < child_code.free_names.size();
                 ++slot) {
                const auto binding = generated.lexical_cell_bindings.find(
                    child_code.free_names[slot]);
                if (binding == generated.lexical_cell_bindings.end())
                    throw std::logic_error(
                        "created closure references an unavailable lexical cell");
                pta_.addFieldStore(binding->second,
                                   generated.value_nodes[instruction_index],
                                   captureField(slot));
            }
            if (isClassBody(child_code)) {
                const auto call_index = classConstructionCall(code, instruction_index);
                if (call_index == program.size() ||
                    generated.allocated_objects[call_index] == 0U)
                    continue;
                const auto class_object = generated.allocated_objects[call_index];
                modeled_receiver_objects_.insert(class_object);
                pta_.addAddressOf(generated.value_nodes[call_index], class_object);
                auto model = class_models_.find(child);
                if (model == class_models_.end()) {
                    const auto class_binding = pta_.newValue();
                    pta_.addAddressOf(class_binding, class_object);
                    const auto instance_object = pta_.newObject();
                    modeled_receiver_objects_.insert(instance_object);
                    const auto instance_binding = pta_.newValue();
                    pta_.addAddressOf(instance_binding, instance_object);
                    pta_.addFieldStore(
                        instance_binding, class_binding,
                        pta_.internField(kPTAClassInstanceFieldName));
                    model = class_models_.emplace(child, ClassModel{
                        {class_object},
                        class_binding,
                        instance_object,
                        instance_binding,
                        {},
                        {},
                        {},
                        {},
                        {},
                        {},
                        {},
                        {},
                        {},
                    }).first;
                    extra_object_origins_.push_back(
                        {instance_object, PTAObjectOriginKind::ClassInstance,
                         child, 0U});
                } else {
                    model->second.class_objects.push_back(class_object);
                    pta_.addAddressOf(model->second.class_binding, class_object);
                    const auto constructor =
                        constructor_targets_by_class_body_.find(child);
                    if (model->second.metaclass_body == 0U &&
                        constructor != constructor_targets_by_class_body_.end())
                        builder_.assignCallable(class_object,
                                                constructor->second);
                }
                class_body_by_object_.insert_or_assign(class_object, child);
                discoverBaseClasses(
                    model->second, generated, call_index);
                discoverMetaclass(
                    code, model->second, generated, call_index);
                if (model->second.metaclass_body != 0U) {
                    const auto metaclass_target =
                        call_targets_by_class_body_.find(
                            model->second.metaclass_body);
                    if (metaclass_target !=
                        call_targets_by_class_body_.end())
                        builder_.assignCallable(
                            class_object, metaclass_target->second);
                }
                const auto body = activate(child, 0U);
                if (collect_observations_)
                    eager_code_executions_.push_back(
                        {code.id, instruction_index, context, child, 0U});
                inheritBaseDefinitions(class_models_.at(child));
                if (body.capture_environment != 0U)
                    pta_.addAddressOf(body.capture_environment, object);
                if (eager_class_bodies_.insert(child).second)
                    builder_.addCallSites(body.call_sites);
                continue;
            }

            const auto [function, inserted] = function_ids_.emplace(
                child, next_function_id_);
            if (inserted) ++next_function_id_;
            const cg::CallableTarget target{function->second, child};
            builder_.registerCallable(object, target);
            if (collect_observations_)
                callable_objects_.push_back({
                    object, target.function, target.code, code.id,
                    instruction_index, context,
                });
            method_may_return_not_implemented_.insert_or_assign(
                object, python::mayReturnNotImplemented(
                            child_code.cfg.program()));
            method_may_miss_known_attribute_.insert_or_assign(
                object, python::mayMissKnownAttribute(
                            child_code.cfg.program()));

            const auto owner = class_models_.find(code.id);
            if (owner == class_models_.end()) continue;
            receiver_objects_by_code_[child] = owner->second.instance_object;
            if (child_code.name == kCallSpecialMethodName) {
                owner->second.instance_call_target = target;
                call_targets_by_class_body_.insert_or_assign(code.id, target);
                metaclass_body_by_call_code_.insert_or_assign(child, code.id);
                builder_.assignCallable(
                    owner->second.instance_object, target);
                for (const auto& entry :
                     owner->second.allocated_instance_bindings)
                    builder_.assignCallable(entry.first, target);
            }
            if (child_code.name == kInitSpecialMethodName) {
                owner->second.own_constructor_target = target;
                owner->second.resolved_constructor_target = target;
                if (owner->second.metaclass_body == 0U)
                    for (const auto class_object : owner->second.class_objects)
                        builder_.assignCallable(class_object, target);
                constructor_class_bodies_[child] = code.id;
                constructor_targets_by_class_body_[code.id] = target;
            }
        }
    }

    std::vector<ArgumentBinding> bindings(const cg::OnTheFlyCallSite& site,
                                          cg::CallableTarget target) {
        const auto& caller = package_.codeObject(site.site.caller);
        const auto& callee = package_.codeObject(target.code);
        if (site.instruction_index >= caller.cfg.program().size())
            throw std::logic_error("PTA call-site instruction index is out of range");
        const auto& call = caller.cfg.program()[site.instruction_index];
        auto formal_names = parameterNames(callee);
        auto positional_count = static_cast<std::size_t>(callee.argument_count);
        auto positional_only_count =
            static_cast<std::size_t>(callee.positional_only_argument_count);
        std::size_t formal_offset = 0;

        const auto receiver = receiver_objects_by_code_.find(target.code);
#ifndef CPYGRAPH_ABLATE_BOUND_METHOD
        if (receiver != receiver_objects_by_code_.end()) {
            const auto constructor = constructor_class_bodies_.find(target.code);
            const auto constructor_model = constructor ==
                    constructor_class_bodies_.end()
                ? class_models_.end() : class_models_.find(constructor->second);
            const bool constructs_instance =
                constructor_model != class_models_.end() &&
                std::any_of(
                    constructor_model->second.class_objects.begin(),
                    constructor_model->second.class_objects.end(),
                    [&](ObjectId class_object) {
                        return pta_.pointsTo(site.site.callee_value).count(
                                   class_object) != 0U;
                    });
            if (constructs_instance) {
                const auto allocation = call_result_allocations_.find(
                    site.call_result);
                const auto instance_object =
                    hasSensitivity(sensitivity_.attributes(target.code),
                                   PTASensitivity::Context) &&
                            allocation != call_result_allocations_.end()
                        ? allocation->second
                        : constructor_model->second.instance_object;
                pta_.addAddressOf(site.call_result, instance_object);
            }

            const bool carries_receiver =
                call.opcode == bytecode::SemanticOpcode::Call &&
                site.actual_arguments.size() > call.operand;
            if (!carries_receiver && !formal_names.empty()) {
                formal_names.erase(formal_names.begin());
                formal_offset = 1U;
                if (positional_count != 0U) --positional_count;
                if (positional_only_count != 0U) --positional_only_count;
            }
        }
#else
        static_cast<void>(receiver);
#endif

        auto result = bindArguments(
            call, site.actual_arguments.size(), formal_names, positional_count,
            positional_only_count, callee.keyword_only_argument_count,
            callee.has_var_arguments, callee.has_var_keywords);
        if (formal_offset != 0U)
            for (auto& binding : result) binding.second += formal_offset;

        const auto target_context = target_contexts_.find(
            SiteTargetKey{site.site.id, target.code});
        if (target_context == target_contexts_.end())
            throw std::logic_error(
                "call target has no selected PTA context");
        const auto activation = activations_.find(
            ActivationKey{target.code, target_context->second});
        if (activation == activations_.end())
            throw std::logic_error(
                "call target has no activated PTA body");
        const ActivationKey activation_key{target.code, target_context->second};
        const auto constructed =
            constructed_class_by_activation_.find(activation_key);
        if (constructed != constructed_class_by_activation_.end())
            wireMetaclassConstructor(
                activation_key, activation->second, constructed->second);
        std::unordered_set<std::size_t> bound_formals;
        for (const auto& binding : result)
            bound_formals.insert(binding.second);
        const auto positional_formals =
            static_cast<std::size_t>(callee.argument_count);
        bool has_missing_positional = false;
        for (std::size_t formal = formal_offset;
             formal < positional_formals; ++formal)
            has_missing_positional |= bound_formals.count(formal) == 0U;
        if (has_missing_positional) {
            const auto attributes = pta_.newValue();
            const auto defaults = pta_.newValue();
            pta_.addFieldLoad(
                site.site.callee_value,
                pta_.internField(kPTAFunctionAttributeFieldName), attributes);
            pta_.addCopy(attributes, defaults);
            pta_.addFieldLoad(attributes, kSummaryFieldId, defaults);
            for (std::size_t formal = formal_offset;
                 formal < positional_formals &&
                 formal < activation->second.formal_parameters.size();
                 ++formal)
                if (bound_formals.count(formal) == 0U)
                    pta_.addCopy(
                        defaults,
                        activation->second.formal_parameters[formal]);
        }
        if (collect_observations_) {
            const auto observed = std::find_if(
                call_activations_.begin(), call_activations_.end(),
                [&](const auto& candidate) {
                    return candidate.site == site.site.id &&
                           candidate.caller == site.site.caller &&
                           candidate.instruction_index ==
                               site.instruction_index &&
                           candidate.caller_context == site.caller_context &&
                           candidate.target == target.code &&
                           candidate.context == target_context->second;
                });
            if (observed == call_activations_.end())
                throw std::logic_error(
                    "resolved call binding has no activation observation");
            observed->bindings = result;
        }
        return result;
    }

    const PackageAnalysis& package_;
    SensitivityPolicy sensitivity_;
    AndersenPointerAnalysis pta_;
    cg::OnTheFlyCallGraphBuilder builder_;
    std::deque<ConstraintInstance> constraints_;
    std::unordered_map<ActivationKey, cg::CallableBodySummary,
                       ActivationKeyHash> activations_;
    std::unordered_map<CallContextKey, PTAContextId,
                       CallContextKeyHash> call_contexts_;
    std::unordered_map<PTAContextId, CallString> context_call_strings_;
    std::unordered_map<SiteTargetKey, PTAContextId, SiteTargetKeyHash>
        target_contexts_;
    std::unordered_set<std::uint64_t> materialized_call_fallback_sites_;
    std::unordered_map<std::uint64_t, python::ProtocolMethodSelection>
        materialized_protocol_methods_;
    std::unordered_map<std::uint64_t, bytecode::PythonMethodSet>
        original_protocol_methods_;
    std::vector<PackageCallActivationObservation> call_activations_;
    std::vector<PackageEagerCodeExecutionObservation>
        eager_code_executions_;
    std::vector<PackageCallableObjectObservation> callable_objects_;
    ModuleObjectIndex module_objects_;
    std::unordered_map<std::string, ObjectId> module_objects_by_name_;
    std::unordered_map<ObjectId, std::string> module_name_by_object_;
    std::unordered_set<std::string> analyzed_module_names_;
    std::unordered_map<std::string, NodeId> module_bindings_;
    ValuesBySymbol global_definitions_;
    ValuesBySymbol global_inputs_;
    std::vector<std::pair<std::string, NodeId>> pending_star_imports_;
    std::unordered_map<CodeObjectId, FunctionId> function_ids_;
    std::unordered_map<CodeObjectId, ClassModel> class_models_;
    std::unordered_map<ObjectId, CodeObjectId> class_body_by_object_;
    std::unordered_map<ObjectId, CodeObjectId>
        super_owner_by_callable_object_;
    std::unordered_map<CodeObjectId, cg::CallableTarget>
        call_targets_by_class_body_;
    std::unordered_map<CodeObjectId, CodeObjectId>
        metaclass_body_by_call_code_;
    std::unordered_map<ActivationKey, CodeObjectId, ActivationKeyHash>
        constructed_class_by_activation_;
    std::unordered_map<ActivationKey, ObjectId, ActivationKeyHash>
        metaclass_constructor_dispatch_objects_;
    std::unordered_set<ObjectId> materialized_super_proxies_;
    std::unordered_set<ObjectId> identity_decorator_objects_;
    std::unordered_set<ObjectId> property_constructor_objects_;
    std::unordered_map<ObjectId, PropertyModel> property_models_;
    std::unordered_map<ObjectId, ObjectId> property_by_setter_callable_;
    std::vector<std::pair<std::uint64_t, NodeId>>
        materialized_property_dispatches_;
    std::unordered_set<std::uint64_t> deferred_property_setter_sites_;
    std::unordered_map<ObjectId, bytecode::PythonMethodSet>
        builtin_method_by_callable_object_;
    struct ObjectFieldHash {
        std::size_t operator()(
            const std::pair<ObjectId, FieldId>& key) const noexcept {
            const auto object = std::hash<ObjectId>{}(key.first);
            const auto field = std::hash<FieldId>{}(key.second);
            return object ^ (field + (object << 6U) + (object >> 2U));
        }
    };
    std::unordered_map<std::pair<ObjectId, FieldId>, ObjectId,
                       ObjectFieldHash>
        external_callable_objects_;
    std::unordered_map<ObjectId, bytecode::PythonExternalCallee>
        external_callee_by_callable_object_;
    std::unordered_map<ObjectId, std::string>
        qualified_target_by_callable_object_;
    std::unordered_set<std::string> requested_external_targets_;
    std::unordered_map<ObjectId, bool>
        method_may_return_not_implemented_;
    std::unordered_map<ObjectId, bool> method_may_miss_known_attribute_;
    std::unordered_set<ObjectId> modeled_receiver_objects_;
    std::unordered_map<CodeObjectId, ObjectId> receiver_objects_by_code_;
    std::unordered_map<ActivationKey, ObjectId, ActivationKeyHash>
        receiver_objects_by_activation_;
    std::unordered_map<NodeId, ObjectId> call_result_allocations_;
    std::unordered_map<CodeObjectId, CodeObjectId> constructor_class_bodies_;
    std::unordered_map<CodeObjectId, cg::CallableTarget>
        constructor_targets_by_class_body_;
    std::unordered_set<CodeObjectId> eager_class_bodies_;
    std::unordered_set<CodeObjectId> external_entry_points_;
    std::vector<PTAObjectOrigin> extra_object_origins_;
    bool globals_finalized_{false};
    bool collect_observations_{false};
    FunctionId next_function_id_{kFirstFunctionId};
    PTAContextId next_context_id_{kFirstPTAContextId};
};

}  // namespace

PackageCallGraphResult OnTheFlyCallGraphAnalyzer::analyze(
    const PackageAnalysis& package,
    const std::vector<CodeObjectId>& entry_points,
    PackageAnalysisObservations* observations,
    const PTASensitivityConfiguration& sensitivity,
    const ExternalCallConfiguration& external_calls) const {
    return Coordinator(package, sensitivity, external_calls)
        .build(entry_points, observations);
}

}  // namespace cpygraph::package
