#include "analysis/pta/semantic_pta.h"
#include "analysis/python/protocol.h"

#include <algorithm>
#include <array>
#include <deque>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace cpygraph {
namespace {

using Values = std::set<NodeId>;
struct State {
    bool initialized{false};
    std::vector<Values> stack;
    std::unordered_map<std::string, Values> locals;
    std::unordered_map<std::string, Values> globals;
};

bool merge(Values& target, const Values& source) {
    const auto size = target.size();
    target.insert(source.begin(), source.end());
    return size != target.size();
}

bool merge(State& target, const State& source, std::uint32_t source_offset,
           std::uint32_t join_offset) {
    if (!source.initialized) return false;
    if (!target.initialized) { target = source; return true; }
    if (target.stack.size() != source.stack.size())
        throw std::runtime_error("PTA operand-stack height mismatch at CFG join at bytecode offset " +
            std::to_string(join_offset) + ": expected " + std::to_string(target.stack.size()) +
            ", received " + std::to_string(source.stack.size()) + " from block at bytecode offset " +
            std::to_string(source_offset));
    bool changed = false;
    for (std::size_t i = 0; i < target.stack.size(); ++i) changed |= merge(target.stack[i], source.stack[i]);
    for (const auto& [name, values] : source.locals) changed |= merge(target.locals[name], values);
    for (const auto& [name, values] : source.globals) changed |= merge(target.globals[name], values);
    return changed;
}

Values pop(State& state, std::uint32_t offset) {
    if (state.stack.empty())
        throw std::runtime_error("PTA operand-stack underflow at bytecode offset " + std::to_string(offset));
    auto result = std::move(state.stack.back());
    state.stack.pop_back();
    return result;
}

void requireStack(const State& state, std::size_t required, std::uint32_t offset,
                  const char* operation) {
    if (required <= state.stack.size()) return;
    throw std::runtime_error(std::string("PTA operand-stack underflow for ") + operation +
        " at bytecode offset " + std::to_string(offset) + ": requires " +
        std::to_string(required) + ", has " + std::to_string(state.stack.size()));
}

std::optional<std::string> relativeModuleName(
    const std::string& current, bool is_package, std::uint32_t level,
    const std::string& imported) {
    if (level == 0U) return imported;
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin <= current.size()) {
        const auto separator = current.find('.', begin);
        parts.push_back(current.substr(begin, separator - begin));
        if (separator == std::string::npos) break;
        begin = separator + 1U;
    }
    if (!is_package && !parts.empty()) parts.pop_back();
    if (parts.empty()) return std::nullopt;
    for (std::uint32_t parent = 1; parent < level; ++parent) {
        if (parts.empty()) return std::nullopt;
        parts.pop_back();
    }
    if (parts.empty()) return std::nullopt;
    std::string result;
    for (const auto& part : parts) {
        if (!result.empty()) result += '.';
        result += part;
    }
    if (!imported.empty()) {
        if (!result.empty()) result += '.';
        result += imported;
    }
    return result;
}

std::string importedModuleIdentity(
    const bytecode::SemanticInstruction& instruction,
    const std::string& current, bool is_package) {
    if (instruction.import_level && *instruction.import_level != 0U) {
        const auto resolved = relativeModuleName(
            current, is_package, *instruction.import_level,
            instruction.symbol);
        if (resolved) return *resolved;
        return std::string("\x1einvalid-relative:") + current + ":" +
               std::to_string(*instruction.import_level) + ":" +
               instruction.symbol;
    }
    if (instruction.import_fromlist) return instruction.symbol;
    const auto separator = instruction.symbol.find('.');
    return separator == std::string::npos
        ? instruction.symbol : instruction.symbol.substr(0, separator);
}

PTAImportRequest importRequest(
    const bytecode::SemanticInstruction& instruction,
    const std::string& current, bool is_package,
    std::size_t instruction_index) {
    std::optional<std::string> module = instruction.symbol;
    if (instruction.import_level && *instruction.import_level != 0U)
        module = relativeModuleName(
            current, is_package, *instruction.import_level,
            instruction.symbol);
    const bool valid = module.has_value();
    return {instruction_index, module.value_or(std::string{}),
            instruction.import_from_names, valid, instruction.symbol,
            instruction.import_level.value_or(0U)};
}

cg::UnresolvedCalleeGroup unresolvedGroup(
    const python::ProtocolDispatch& dispatch) {
    return {
        cg::UnresolvedCalleeKind::PythonProtocol,
        dispatch.operation,
        dispatch.candidate_methods,
        dispatch.external_callee,
    };
}

void mergeGroup(cg::UnresolvedCalleeGroup& target,
                const cg::UnresolvedCalleeGroup& source) {
    if (target.kind == cg::UnresolvedCalleeKind::DynamicCall) target = source;
    if (target.operation != source.operation)
        target.operation = bytecode::PythonProtocolOperation::DynamicProtocol;
    target.candidate_methods |= source.candidate_methods;
    if (target.external_callee == bytecode::PythonExternalCallee::None)
        target.external_callee = source.external_callee;
    else if (source.external_callee != bytecode::PythonExternalCallee::None &&
             target.external_callee != source.external_callee)
        target.external_callee = bytecode::PythonExternalCallee::None;
}

void recordProtocolReceivers(
    PTAConstraintResult& result, std::size_t instruction_index,
    const Values& primary, const Values& secondary = {}) {
    const auto found = std::find_if(
        result.protocol_receivers.begin(), result.protocol_receivers.end(),
        [instruction_index](const auto& receivers) {
            return receivers.instruction_index == instruction_index;
        });
    auto& receivers = found == result.protocol_receivers.end()
        ? result.protocol_receivers.emplace_back(
              PTAProtocolReceiverSet{instruction_index, {}, {}})
        : *found;
    for (const auto value : primary)
        if (std::find(receivers.primary.begin(), receivers.primary.end(),
                      value) == receivers.primary.end())
            receivers.primary.push_back(value);
    for (const auto value : secondary)
        if (std::find(receivers.secondary.begin(), receivers.secondary.end(),
                      value) == receivers.secondary.end())
            receivers.secondary.push_back(value);
}

}  // namespace

PTAConstraintResult SemanticPTAConstraintBuilder::build(const cfg::ControlFlowGraph& cfg,
                                                         CodeObjectId caller,
                                                         const std::vector<std::string>& parameters,
                                                         const std::vector<std::string>& free_names) {
    if (caller == 0) throw std::invalid_argument("PTA caller id zero is reserved");
    std::unordered_set<std::string> unique_parameters;
    for (const auto& parameter : parameters) {
        if (parameter.empty() || !unique_parameters.insert(parameter).second)
            throw std::invalid_argument("PTA parameter names must be nonempty and unique");
    }
    std::unordered_set<std::string> free_variable_names;
    for (const auto& name : free_names) {
        if (name.empty() || !free_variable_names.insert(name).second)
            throw std::invalid_argument(
                "PTA free-variable names must be nonempty and unique");
    }
    PTAConstraintResult result;
    const auto& program = cfg.program();
    result.value_nodes.resize(program.size());
    result.secondary_value_nodes.resize(program.size());
    for (std::size_t i = 0; i < program.size(); ++i) {
        result.value_nodes[i] = analysis_.newValue();
        using O = bytecode::SemanticOpcode;
        if (program[i].opcode == O::LoadLocalPair ||
            program[i].opcode == O::StoreLocalPair ||
            program[i].opcode == O::StoreLoadLocal)
            result.secondary_value_nodes[i] = analysis_.newValue();
    }
    result.attribute_load_bases.resize(program.size());
    result.attribute_store_bases.resize(program.size());
    result.attribute_store_values.resize(program.size());
    result.element_load_bases.resize(program.size());
    result.element_load_keys.resize(program.size());
    std::vector<NodeId> call_callees(program.size(), 0);
    std::vector<std::vector<std::vector<NodeId>>> call_arguments(program.size());
    std::vector<std::size_t> call_explicit_argument_offsets(
        program.size(), 0U);
    std::vector<std::vector<NodeId>> call_callee_inputs(program.size());
    std::vector<cg::UnresolvedCalleeGroup> call_groups(program.size());
    std::unordered_map<NodeId, cg::UnresolvedCalleeGroup> value_groups;
    std::vector<ObjectId> allocation_objects(program.size(), 0);
    std::unordered_map<std::uint32_t, ObjectId> constant_objects;
    std::unordered_map<std::string, std::vector<ObjectId>> module_objects;
    for (std::size_t i = 0; i < program.size(); ++i) {
        const auto& instruction = program[i];
        if (python::mayInvokeUserCode(instruction) ||
            (instruction.opcode == bytecode::SemanticOpcode::Raise &&
             instruction.stack_input_count != 0U)) {
            call_callees[i] = analysis_.newValue();
            analysis_.addUnknown(call_callees[i]);
            call_groups[i] = unresolvedGroup(python::protocolDispatch(instruction));
        }
        if (instruction.opcode == bytecode::SemanticOpcode::LoadConst) {
            auto [entry, inserted] = constant_objects.emplace(instruction.operand, 0);
            if (inserted) entry->second = analysis_.newObject();
            allocation_objects[i] = entry->second;
        } else if (instruction.opcode == bytecode::SemanticOpcode::ImportModule) {
            const auto identity = importedModuleIdentity(
                instruction, module_name_, module_is_package_);
            auto [entry, inserted] = module_objects.emplace(
                identity, std::vector<ObjectId>{});
            if (inserted) {
                if (module_objects_) {
                    const auto exact = module_objects_->exact.find(identity);
                    if (exact != module_objects_->exact.end()) entry->second = exact->second;
                    if (!instruction.import_level && identity == instruction.symbol) {
                        const auto suffix = module_objects_->suffix.find(instruction.symbol);
                        if (suffix != module_objects_->suffix.end())
                            for (const auto object : suffix->second)
                                if (std::find(entry->second.begin(), entry->second.end(), object) ==
                                    entry->second.end())
                                    entry->second.push_back(object);
                    }
                }
                if (entry->second.empty()) entry->second.push_back(analysis_.newObject());
            }
            allocation_objects[i] = entry->second.front();
        } else if (instruction.opcode == bytecode::SemanticOpcode::LoadLiteral ||
                   instruction.opcode == bytecode::SemanticOpcode::BuildCollection ||
                   (instruction.opcode == bytecode::SemanticOpcode::Generic &&
                    instruction.fresh_result) ||
                   instruction.opcode == bytecode::SemanticOpcode::Call ||
                   (instruction.opcode == bytecode::SemanticOpcode::Raise &&
                    instruction.stack_input_count != 0U) ||
                   instruction.opcode == bytecode::SemanticOpcode::CreateFunction ||
                   instruction.implicit_constant) {
            allocation_objects[i] = analysis_.newObject();
        }
    }
    for (std::size_t i = 0U; i < program.size(); ++i)
        if (program[i].opcode == bytecode::SemanticOpcode::LoadConst &&
            program[i].constant_integer && allocation_objects[i] != 0U)
            analysis_.bindFieldKey(
                allocation_objects[i],
                analysis_.internIndexField(*program[i].constant_integer));
    if (cfg.blocks().empty()) {
        result.allocated_objects = std::move(allocation_objects);
        return result;
    }
    std::vector<State> incoming(cfg.blocks().size() + 1);
    incoming[1].initialized = true;
    const auto seed_unknown = [&](const std::string& name, bool local) {
        auto& names = local ? incoming[1].locals : incoming[1].globals;
        if (name.empty() || names.count(name)) return;
        const auto entry_value = analysis_.newValue();
        if (local || !options_.defer_global_unknowns)
            analysis_.addUnknown(entry_value);
        names.emplace(name, Values{entry_value});
        if (local) result.local_inputs[name].push_back(entry_value);
        else result.global_inputs[name].push_back(entry_value);
    };
    for (const auto& instruction : program) {
        using O = bytecode::SemanticOpcode;
        if (instruction.opcode == O::LoadLocal &&
            instruction.lexical_access ==
                bytecode::LexicalAccessKind::FastLocal)
            seed_unknown(instruction.symbol, true);
        else if (instruction.opcode == O::LoadGlobal)
            seed_unknown(instruction.symbol, false);
        else if (instruction.opcode == O::LoadLocalPair) {
            seed_unknown(instruction.symbol, true);
            seed_unknown(instruction.secondary_symbol, true);
        } else if (instruction.opcode == O::StoreLoadLocal)
            seed_unknown(instruction.secondary_symbol, true);
    }
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const auto& parameter = parameters[index];
        const auto formal = analysis_.newValue();
        // Entry parameters need an abstract object even when no in-artifact
        // caller is known. Methods of one class share a receiver abstraction
        // so fields written in one method remain visible in another; unrelated
        // parameters keep distinct external abstractions.
        if (index == 0 && receiver_object_ != 0 &&
            (parameter == "self" || parameter == "cls"))
            analysis_.addAddressOf(formal, receiver_object_);
        else if (options_.seed_external_parameters)
            analysis_.addAddressOf(formal, analysis_.newObject());
        incoming[1].locals[parameter] = {formal};
        result.formal_parameters.push_back(formal);
    }
    const auto cell_contents = analysis_.internField(kPTACellContentsFieldName);
    const auto ensure_cell = [&](State& state, const std::string& name) {
        const auto existing = result.lexical_cell_bindings.find(name);
        if (existing != result.lexical_cell_bindings.end())
            return existing->second;

        const auto binding = analysis_.newValue();
        result.lexical_cell_bindings.emplace(name, binding);
        // A local cell belongs to this function activation. A free-variable
        // binding receives the parent cell object later through the concrete
        // closure object's capture field.
        if (free_variable_names.count(name) == 0U) {
            analysis_.addAddressOf(binding, analysis_.newObject());
            const auto local = state.locals.find(name);
            if (local != state.locals.end())
                for (const auto value : local->second)
                    analysis_.addFieldStore(value, binding, cell_contents);
        }
        return binding;
    };
    for (const auto& name : free_names) ensure_cell(incoming[1], name);
    std::unordered_map<std::string, NodeId> local_summaries;
    std::unordered_map<std::string, NodeId> global_summaries;
    const auto flow_insensitive_summary = [&](const std::string& name,
                                               bool local) {
        auto& summaries = local ? local_summaries : global_summaries;
        const auto found = summaries.find(name);
        if (found != summaries.end()) return found->second;
        const auto summary = analysis_.newValue();
        summaries.emplace(name, summary);
        const auto& values = local ? incoming[1].locals : incoming[1].globals;
        const auto entry = values.find(name);
        if (entry != values.end())
            for (const auto value : entry->second)
                analysis_.addCopy(value, summary);
        return summary;
    };
    std::deque<cfg::BlockId> work{1};
    std::vector<bool> queued(cfg.blocks().size() + 1, false);
    queued[1] = true;
    std::unordered_map<cfg::BlockId, std::vector<NodeId>> exception_inputs;
    std::unordered_map<cfg::BlockId, std::vector<NodeId>> protected_stack_inputs;
    std::vector<bool> visited_instructions(program.size(), false);

    while (!work.empty()) {
        const auto block_id = work.front();
        work.pop_front();
        queued[block_id] = false;
        State state = incoming[block_id];
        for (const auto index : cfg.block(block_id).instruction_indices) {
            visited_instructions[index] = true;
            const auto& instruction = program[index];
            const auto current = result.value_nodes[index];
            using O = bytecode::SemanticOpcode;
            switch (instruction.opcode) {
                case O::Nop:
                    if (instruction.lexical_access ==
                        bytecode::LexicalAccessKind::CellCreation)
                        ensure_cell(state, instruction.symbol);
                    break;
                case O::PrepareKeywordCall:
                case O::Branch:
                    break;
                case O::LoadConst:
                case O::LoadLiteral:
                    analysis_.addAddressOf(current, allocation_objects[index]);
                    state.stack.push_back({current});
                    break;
                case O::BuildCollection: {
                    requireStack(state, instruction.stack_input_count,
                                 instruction.offset, "collection construction");
                    std::vector<Values> elements;
                    elements.reserve(instruction.stack_input_count);
                    for (std::uint32_t input = 0;
                         input < instruction.stack_input_count; ++input)
                        elements.push_back(pop(state, instruction.offset));
                    analysis_.addAddressOf(current, allocation_objects[index]);
                    for (std::size_t popped = 0U; popped < elements.size();
                         ++popped) {
                        const auto position = elements.size() - popped - 1U;
                        const auto field = instruction.positional_collection
                            ? analysis_.internIndexField(
                                  static_cast<std::int64_t>(position))
                            : FieldId{0U};
                        const auto& values = elements[popped];
                        for (const auto value : values)
                            analysis_.addFieldStore(value, current, field);
                    }
                    state.stack.push_back({current});
                    break;
                }
                case O::UnpackCollection: {
                    const auto bases = pop(state, instruction.offset);
                    constexpr std::uint32_t kUnpackBeforeMask = 0xffU;
                    const bool starred =
                        instruction.operand != instruction.stack_output_count;
                    const auto before = instruction.operand & kUnpackBeforeMask;
                    std::vector<NodeId> outputs;
                    outputs.reserve(instruction.stack_output_count);
                    for (std::uint32_t position = 0U;
                         position < instruction.stack_output_count;
                         ++position) {
                        const auto output = position == 0U
                            ? current : analysis_.newValue();
                        for (const auto base : bases)
                            analysis_.addFieldLoad(
                                base,
                                starred && position >= before
                                    ? FieldId{0U}
                                    : analysis_.internIndexField(
                                          static_cast<std::int64_t>(position)),
                                output);
                        outputs.push_back(output);
                    }
                    for (auto output = outputs.rbegin();
                         output != outputs.rend(); ++output)
                        state.stack.push_back({*output});
                    break;
                }
                case O::CallProtocolMarker:
                    state.stack.push_back({});
                    break;
                case O::Generic:
                case O::Yield:
                case O::Suspend: {
                    requireStack(state, instruction.stack_peek_count,
                                 instruction.offset, "generic peek");
                    requireStack(state, instruction.stack_input_count,
                                 instruction.offset, "generic input");
                    Values peeked_values;
                    for (std::uint32_t peeked = 0;
                         peeked < instruction.stack_peek_count; ++peeked) {
                        for (const auto source :
                             state.stack[state.stack.size() - 1U - peeked]) {
                            analysis_.addCopy(source, current);
                            peeked_values.insert(source);
                        }
                    }
                    std::vector<Values> consumed;
                    for (std::uint32_t input = 0;
                         input < instruction.stack_input_count; ++input) {
                        consumed.push_back(pop(state, instruction.offset));
                        for (const auto source : consumed.back())
                            analysis_.addCopy(source, current);
                    }
                    if (instruction.protocol_operation ==
                            bytecode::PythonProtocolOperation::Import)
                        for (const auto& values : consumed)
                            result.star_import_sources.insert(
                                result.star_import_sources.end(),
                                values.begin(), values.end());
                    if (instruction.protocol_methods &&
                        (!consumed.empty() || !peeked_values.empty())) {
                        const auto& left = consumed.empty()
                            ? peeked_values : consumed.back();
                        const auto& right = consumed.empty()
                            ? peeked_values : consumed.front();
                        const bool contains =
                            bytecode::containsPythonMethod(
                                instruction.protocol_methods,
                                bytecode::PythonSpecialMethod::Contains);
                        const auto& primary = contains ? right : left;
                        const auto& secondary = contains ? left : right;
                        recordProtocolReceivers(
                            result, index, primary,
                            consumed.size() > 1U ? secondary : Values{});
                        if (consumed.size() > 1U) {
                            Values other_operands = left;
                            other_operands.insert(right.begin(), right.end());
                            call_arguments[index].push_back(
                                {other_operands.begin(), other_operands.end()});
                        }
                    }
                    if (instruction.fresh_result)
                        analysis_.addAddressOf(current, allocation_objects[index]);
                    else if (instruction.stack_output_count != 0 &&
                             !instruction.provenance_preserving)
                        analysis_.addUnknown(current);
                    if (instruction.opcode == O::Yield)
                        result.return_values.push_back(current);
                    for (std::uint32_t output = 0; output < instruction.stack_output_count; ++output)
                        state.stack.push_back({current});
                    break;
                }
                case O::StackCopy: {
                    if (instruction.operand == 0)
                        throw std::runtime_error("PTA stack-copy depth zero at bytecode offset " +
                                                 std::to_string(instruction.offset));
                    requireStack(state, instruction.operand, instruction.offset, "stack copy");
                    const auto& sources = state.stack[state.stack.size() - instruction.operand];
                    for (const auto source : sources) analysis_.addCopy(source, current);
                    for (const auto source : sources) {
                        const auto group = value_groups.find(source);
                        if (group != value_groups.end())
                            mergeGroup(value_groups[current], group->second);
                    }
                    state.stack.push_back({current});
                    break;
                }
                case O::StackSwap:
                    if (instruction.operand == 0)
                        throw std::runtime_error("PTA stack-swap depth zero at bytecode offset " +
                                                 std::to_string(instruction.offset));
                    requireStack(state, instruction.operand, instruction.offset, "stack swap");
                    std::swap(state.stack.back(), state.stack[state.stack.size() - instruction.operand]);
                    break;
                case O::StackRotate:
                    if (instruction.operand == 0)
                        throw std::runtime_error("PTA stack-rotate depth zero at bytecode offset " +
                                                 std::to_string(instruction.offset));
                    requireStack(state, instruction.operand, instruction.offset, "stack rotate");
                    std::rotate(state.stack.end() - instruction.operand,
                                state.stack.end() - 1, state.stack.end());
                    break;
                case O::LoadElement: {
                    const auto keys = pop(state, instruction.offset);
                    const auto bases = pop(state, instruction.offset);
                    auto& recorded_bases = result.element_load_bases[index];
                    auto& recorded_keys = result.element_load_keys[index];
                    recorded_bases.assign(bases.begin(), bases.end());
                    recorded_keys.assign(keys.begin(), keys.end());
                    for (const auto base : bases)
                        for (const auto key : keys)
                            analysis_.addKeyedFieldLoad(base, key, current);
                    analysis_.addUnknown(current);
                    state.stack.push_back({current});
                    break;
                }
                case O::StoreElement: {
                    pop(state, instruction.offset);  // key
                    const auto bases = pop(state, instruction.offset);
                    const auto values = pop(state, instruction.offset);
                    for (const auto base : bases)
                        for (const auto value : values)
                            analysis_.addFieldStore(value, base, FieldId{0});
                    break;
                }
                case O::StoreCollectionElement: {
                    std::vector<Values> stored;
                    for (std::uint32_t input = 0;
                         input < instruction.stack_input_count; ++input)
                        stored.push_back(pop(state, instruction.offset));
                    if (instruction.operand == 0 ||
                        instruction.operand > state.stack.size())
                        throw std::runtime_error(
                            "PTA collection-update depth is out of range at bytecode offset " +
                            std::to_string(instruction.offset));
                    const auto& bases =
                        state.stack[state.stack.size() - instruction.operand];
                    for (const auto base : bases)
                        for (const auto& values : stored)
                            for (const auto value : values)
                                analysis_.addFieldStore(value, base, FieldId{0});
                    break;
                }
                case O::DeleteElement:
                    pop(state, instruction.offset);
                    pop(state, instruction.offset);
                    break;
                case O::ImportModule:
                    for (std::uint32_t i = 0; i < instruction.discarded_stack_values; ++i)
                        pop(state, instruction.offset);
                    for (const auto object : module_objects.at(
                             importedModuleIdentity(
                                 instruction, module_name_, module_is_package_)))
                        analysis_.addAddressOf(current, object);
                    state.stack.push_back({current});
                    break;
                case O::ImportAttribute: {
                    if (state.stack.empty()) pop(state, instruction.offset);
                    const auto field = analysis_.internField(instruction.symbol);
                    auto& recorded_bases =
                        result.attribute_load_bases[index];
                    for (const auto module : state.stack.back())
                        if (std::find(
                                recorded_bases.begin(), recorded_bases.end(),
                                module) == recorded_bases.end())
                            recorded_bases.push_back(module);
                    for (const auto module : state.stack.back())
                        analysis_.addFieldLoad(module, field, current);
                    analysis_.addUnknown(current);
                    state.stack.push_back({current});
                    break;
                }
                case O::EnterContext: {
                    const auto managers = pop(state, instruction.offset);
                    const auto exit_callable = analysis_.newValue();
                    const auto enter_field = analysis_.internField("__enter__");
                    const auto exit_field = analysis_.internField("__exit__");
                    for (const auto manager : managers) {
                        analysis_.addFieldLoad(manager, enter_field,
                                               call_callees[index]);
                        analysis_.addFieldLoad(manager, exit_field,
                                               exit_callable);
                    }
                    // Descriptors, metaclasses, and runtime replacement can
                    // still produce targets outside the analyzed package.
                    analysis_.addUnknown(current);
                    analysis_.addUnknown(exit_callable);
                    value_groups[exit_callable] = {
                        cg::UnresolvedCalleeKind::PythonProtocol,
                        bytecode::PythonProtocolOperation::ContextExit,
                        bytecode::pythonMethod(
                            bytecode::PythonSpecialMethod::Exit),
                    };
                    // CPython retains __exit__ below the __enter__ result.
                    state.stack.push_back({exit_callable});
                    state.stack.push_back({current});
                    break;
                }
                case O::LoadLocal:
                case O::LoadGlobal: {
                    const bool local = instruction.opcode == O::LoadLocal;
                    if (local && instruction.lexical_access !=
                                     bytecode::LexicalAccessKind::FastLocal) {
                        const auto cell = ensure_cell(state, instruction.symbol);
                        if (instruction.lexical_access ==
                            bytecode::LexicalAccessKind::CellReference) {
                            analysis_.addCopy(cell, current);
                            state.stack.push_back({current});
                        } else {
                            analysis_.addFieldLoad(cell, cell_contents, current);
                            state.stack.push_back({current});
                        }
                        break;
                    }
                    auto& names = local ? state.locals : state.globals;
                    const bool ordered_name =
                        local ? options_.flow_sensitive
                              : options_.ordered_module_globals;
                    if (!ordered_name) {
                        analysis_.addCopy(flow_insensitive_summary(
                                              instruction.symbol, local),
                                          current);
                    } else {
                        const auto found = names.find(instruction.symbol);
                        if (found == names.end() || found->second.empty())
                            analysis_.addUnknown(current);
                        else
                            for (const auto source : found->second)
                                analysis_.addCopy(source, current);
                    }
                    if (instruction.push_call_protocol_marker) state.stack.push_back({});
                    state.stack.push_back({current});
                    if (!local) {
                        const auto builtin = python::builtinProtocolDispatch(
                            instruction.symbol);
                        if (builtin.candidate_methods ||
                            builtin.external_callee !=
                                bytecode::PythonExternalCallee::None)
                            value_groups[current] = unresolvedGroup(builtin);
                    }
                    if (ordered_name && local &&
                        instruction.clear_local_after_load)
                        state.locals.erase(instruction.symbol);
                    break;
                }
                case O::StoreLocal:
                case O::StoreGlobal: {
                    const auto sources = pop(state, instruction.offset);
                    for (const auto source : sources) analysis_.addCopy(source, current);
                    if (instruction.opcode == O::StoreLocal &&
                        instruction.lexical_access ==
                            bytecode::LexicalAccessKind::CellValue) {
                        const auto cell = ensure_cell(state, instruction.symbol);
                        analysis_.addFieldStore(current, cell, cell_contents);
                        break;
                    }
                    auto& names = instruction.opcode == O::StoreLocal ? state.locals : state.globals;
                    names[instruction.symbol] = {current};
                    if ((instruction.opcode == O::StoreLocal &&
                         !options_.flow_sensitive) ||
                        (instruction.opcode == O::StoreGlobal &&
                         !options_.ordered_module_globals))
                        analysis_.addCopy(
                            current,
                            flow_insensitive_summary(
                                instruction.symbol,
                                instruction.opcode == O::StoreLocal));
                    if (instruction.opcode == O::StoreGlobal)
                        result.global_definitions[instruction.symbol].push_back(current);
                    break;
                }
                case O::DeleteLocal:
                    if (instruction.lexical_access ==
                        bytecode::LexicalAccessKind::FastLocal &&
                        options_.flow_sensitive)
                        state.locals.erase(instruction.symbol);
                    break;
                case O::DeleteGlobal:
                    break;
                case O::LoadLocalPair: {
                    const std::array<std::pair<const std::string*, NodeId>, 2U>
                        outputs{{
                            {&instruction.symbol, current},
                            {&instruction.secondary_symbol,
                             result.secondary_value_nodes[index]},
                        }};
                    for (const auto& [name, output] : outputs) {
                        if (!options_.flow_sensitive) {
                            analysis_.addCopy(
                                flow_insensitive_summary(*name, true), output);
                        } else {
                            const auto found = state.locals.find(*name);
                            if (found == state.locals.end() || found->second.empty())
                                analysis_.addUnknown(output);
                            else
                                for (const auto source : found->second)
                                    analysis_.addCopy(source, output);
                        }
                        state.stack.push_back({output});
                    }
                    break;
                }
                case O::StoreLocalPair: {
                    const std::array<std::pair<const std::string*, NodeId>, 2U>
                        outputs{{
                            {&instruction.symbol, current},
                            {&instruction.secondary_symbol,
                             result.secondary_value_nodes[index]},
                        }};
                    for (const auto& [name, output] : outputs) {
                        const auto sources = pop(state, instruction.offset);
                        for (const auto source : sources)
                            analysis_.addCopy(source, output);
                        state.locals[*name] = {output};
                        if (!options_.flow_sensitive)
                            analysis_.addCopy(
                                output, flow_insensitive_summary(*name, true));
                    }
                    break;
                }
                case O::StoreLoadLocal: {
                    const auto stored = pop(state, instruction.offset);
                    for (const auto source : stored) analysis_.addCopy(source, current);
                    state.locals[instruction.symbol] = {current};
                    if (!options_.flow_sensitive) {
                        analysis_.addCopy(
                            current,
                            flow_insensitive_summary(instruction.symbol, true));
                        analysis_.addCopy(
                            flow_insensitive_summary(
                                instruction.secondary_symbol, true),
                            result.secondary_value_nodes[index]);
                    } else {
                        const auto found =
                            state.locals.find(instruction.secondary_symbol);
                        if (found == state.locals.end() || found->second.empty())
                            analysis_.addUnknown(
                                result.secondary_value_nodes[index]);
                        else
                            for (const auto source : found->second)
                                analysis_.addCopy(
                                    source, result.secondary_value_nodes[index]);
                    }
                    state.stack.push_back(
                        {result.secondary_value_nodes[index]});
                    break;
                }
                case O::LoadAttribute: {
                    const auto bases = pop(state, instruction.offset);
                    for (std::uint32_t discarded = 0;
                         discarded < instruction.discarded_stack_values;
                         ++discarded)
                        pop(state, instruction.offset);
                    auto& recorded_bases = result.attribute_load_bases[index];
                    for (const auto base : bases)
                        if (std::find(recorded_bases.begin(), recorded_bases.end(), base) ==
                            recorded_bases.end())
                            recorded_bases.push_back(base);
                    recordProtocolReceivers(result, index, bases);
                    const auto field = instruction.symbol.empty() ? FieldId{0} : analysis_.internField(instruction.symbol);
                    if (!instruction.super_attribute)
                        for (const auto base : bases)
                            analysis_.addFieldLoad(base, field, current);
                    analysis_.addUnknown(current);  // descriptors/dynamic attributes may synthesize a value
                    state.stack.push_back({current});
                    const auto external =
                        python::builtinAttributeCallee(instruction.symbol);
                    if (external != bytecode::PythonExternalCallee::None)
                        value_groups[current] = {
                            cg::UnresolvedCalleeKind::PythonProtocol,
                            bytecode::PythonProtocolOperation::AttributeLoad,
                            {}, external};
                    if (instruction.push_call_receiver) state.stack.push_back(bases);
                    break;
                }
                case O::StoreAttribute: {
                    const auto bases = pop(state, instruction.offset);
                    const auto values = pop(state, instruction.offset);
                    auto& recorded_bases = result.attribute_store_bases[index];
                    auto& recorded_values = result.attribute_store_values[index];
                    for (const auto base : bases)
                        if (std::find(recorded_bases.begin(), recorded_bases.end(), base) ==
                            recorded_bases.end())
                            recorded_bases.push_back(base);
                    recordProtocolReceivers(result, index, bases);
                    for (const auto value : values)
                        if (std::find(recorded_values.begin(), recorded_values.end(), value) ==
                            recorded_values.end())
                            recorded_values.push_back(value);
                    const auto field = instruction.symbol.empty() ? FieldId{0} : analysis_.internField(instruction.symbol);
                    for (const auto base : bases)
                        for (const auto value : values) analysis_.addFieldStore(value, base, field);
                    break;
                }
                case O::CreateFunction: {
                    const auto attribute_field = analysis_.internField(
                        kPTAFunctionAttributeFieldName);
                    for (std::uint32_t i = 0;
                         i < instruction.discarded_stack_values; ++i) {
                        const auto attributes = pop(state, instruction.offset);
                        for (const auto attribute : attributes)
                            analysis_.addFieldStore(
                                attribute, current, attribute_field);
                    }
                    const auto codes = pop(state, instruction.offset);
                    for (std::uint32_t i = 0;
                         i < instruction.auxiliary_input_count; ++i) {
                        const auto attributes = pop(state, instruction.offset);
                        for (const auto attribute : attributes)
                            analysis_.addFieldStore(
                                attribute, current, attribute_field);
                    }
                    const auto function = allocation_objects[index];
                    analysis_.addAddressOf(current, function);
                    const auto code_field = analysis_.internField("__code__");
                    for (const auto code : codes) analysis_.addFieldStore(code, current, code_field);
                    state.stack.push_back({current});
                    break;
                }
                case O::SetFunctionAttribute: {
                    const auto functions = pop(state, instruction.offset);
                    const auto attributes = pop(state, instruction.offset);
                    for (const auto function : functions) {
                        analysis_.addCopy(function, current);
                        const auto field = analysis_.internField(
                            (instruction.operand & 0x08U) != 0U
                                ? "__closure__"
                                : kPTAFunctionAttributeFieldName);
                        for (const auto attribute : attributes)
                            analysis_.addFieldStore(attribute, function, field);
                    }
                    state.stack.push_back({current});
                    break;
                }
                case O::Call: {
                    for (std::uint32_t metadata = 0;
                         metadata < instruction.discarded_stack_values; ++metadata)
                        pop(state, instruction.offset);
                    std::vector<std::vector<NodeId>> arguments;
                    for (std::uint32_t i = 0; i < instruction.operand; ++i) {
                        auto values = pop(state, instruction.offset);
                        if (values.empty()) {
                            const auto unknown = analysis_.newValue();
                            analysis_.addUnknown(unknown);
                            values.insert(unknown);
                        }
                        arguments.emplace_back(values.begin(), values.end());
                    }
                    // Arguments are popped right-to-left; expose them in source order.
                    std::reverse(arguments.begin(), arguments.end());
                    Values callees;
                    if (instruction.call_protocol_input_count == 1) {
                        callees = pop(state, instruction.offset);
                    } else {
                        const auto protocol_top = pop(state, instruction.offset);
                        const auto protocol_bottom = pop(state, instruction.offset);
                        if (protocol_top.empty()) {
                            callees = protocol_bottom;
                        } else if (protocol_bottom.empty()) {
                            callees = protocol_top;
                        } else {
                            callees = protocol_bottom;
                            arguments.insert(arguments.begin(),
                                std::vector<NodeId>(protocol_top.begin(), protocol_top.end()));
                            call_explicit_argument_offsets[index] = 1U;
                        }
                    }
                    if (call_callees[index] == 0) call_callees[index] = analysis_.newValue();
                    call_callee_inputs[index].assign(
                        callees.begin(), callees.end());
                    for (const auto callee : callees) {
                        analysis_.addCopy(callee, call_callees[index]);
                        if (!options_.allocation_sensitive) {
                            const auto class_instance_field =
                                analysis_.internField(kPTAClassInstanceFieldName);
                            analysis_.addFieldLoad(callee, class_instance_field,
                                                   current);
                        }
                        const auto group = value_groups.find(callee);
                        if (group != value_groups.end())
                            mergeGroup(call_groups[index], group->second);
                    }
                    // Keep an allocation-site summary for an external return
                    // as well as Unknown.  The concrete object lets fields,
                    // collections, and aliases of distinct call results stay
                    // separate; interprocedural return constraints can still
                    // add aliases without discarding the conservative top.
                    if (!options_.defer_call_fallbacks) {
                        analysis_.addAddressOf(current, allocation_objects[index]);
                        analysis_.addUnknown(current);
                    }
                    call_arguments[index] = std::move(arguments);
                    state.stack.push_back({current});
                    break;
                }
                case O::Return:
                    if (instruction.implicit_constant) {
                        analysis_.addAddressOf(current, allocation_objects[index]);
                    } else {
                        const auto returned = pop(state, instruction.offset);
                        for (const auto source : returned)
                            analysis_.addCopy(source, current);
                    }
                    // The instruction value is the public PTA observation for
                    // this return site.  Keep the interprocedural summary on
                    // that same node so clients do not have to reconstruct a
                    // returned may-set from preceding calls or stack state.
                    result.return_values.push_back(current);
                    break;
                case O::Raise:
                    if (instruction.stack_input_count != 0U)
                        analysis_.addAddressOf(
                            current, allocation_objects[index]);
                    for (std::uint32_t input = 0;
                         input < instruction.stack_input_count; ++input) {
                        const auto raised = pop(state, instruction.offset);
                        // With an explicit cause, CPython pops the cause first
                        // and the exception second. The final popped value is
                        // therefore the exception class or instance whose
                        // construction may invoke package code.
                        if (input + 1U == instruction.stack_input_count)
                            for (const auto source : raised)
                                analysis_.addCopy(source, call_callees[index]);
                    }
                    break;
                case O::Pop:
                    pop(state, instruction.offset);
                    break;
                case O::ConditionalBranch: {
                    const auto required = std::max({instruction.stack_peek_count,
                                                    bytecode::conditionalStackInputs(instruction, false),
                                                    bytecode::conditionalStackInputs(instruction, true)});
                    requireStack(state, required, instruction.offset, "conditional branch");
                    if (!state.stack.empty())
                        recordProtocolReceivers(
                            result, index, state.stack.back());
                    if (bytecode::conditionalStackOutputs(instruction, false) != 0 ||
                        bytecode::conditionalStackOutputs(instruction, true) != 0) {
                        analysis_.addUnknown(current);
                        if (instruction.protocol_operation ==
                                bytecode::PythonProtocolOperation::Iteration &&
                            !state.stack.empty())
                            for (const auto iterator : state.stack.back()) {
                                analysis_.addCopy(iterator, current);
                                analysis_.addFieldLoad(
                                    iterator, FieldId{0U}, current);
                            }
                    }
                    break;
                }
                case O::Unsupported:
                    analysis_.addUnknown(current);
                    throw std::runtime_error("cannot generate PTA constraints for unsupported semantic opcode");
            }
        }
        const auto& last = program[cfg.block(block_id).instruction_indices.back()];
        for (const auto& edge : cfg.outgoingEdges(block_id)) {
            if (last.opcode == bytecode::SemanticOpcode::ConditionalBranch &&
                edge.kind != cfg::EdgeKind::Exception) {
                const auto instruction_index =
                    cfg.block(block_id).instruction_indices.back();
                const auto decision = std::find_if(
                    options_.path_decisions.begin(),
                    options_.path_decisions.end(),
                    [&](const PTAPathDecision& current) {
                        return current.instruction_index == instruction_index;
                    });
                if (decision != options_.path_decisions.end()) {
                    const bool jump_edge = edge.kind == (last.jump_on_true
                        ? cfg::EdgeKind::BranchTrue
                        : cfg::EdgeKind::BranchFalse);
                    if (jump_edge != decision->take_jump) continue;
                }
            }
            State outgoing = state;
            if (edge.kind == cfg::EdgeKind::Exception) {
                while (outgoing.stack.size() < edge.stack_depth) {
                    auto& values = protected_stack_inputs[edge.target];
                    const auto index = outgoing.stack.size();
                    while (values.size() <= index) {
                        const auto unknown = analysis_.newValue();
                        analysis_.addUnknown(unknown);
                        values.push_back(unknown);
                    }
                    outgoing.stack.push_back({values[index]});
                }
                outgoing.stack.resize(edge.stack_depth);
                auto& values = exception_inputs[edge.target];
                while (values.size() < edge.exception_stack_items) {
                    const auto unknown = analysis_.newValue();
                    analysis_.addUnknown(unknown);
                    values.push_back(unknown);
                }
                for (std::uint32_t item = 0; item < edge.exception_stack_items; ++item)
                    outgoing.stack.push_back({values[item]});
            } else if (last.opcode == bytecode::SemanticOpcode::ConditionalBranch) {
                const bool jump_edge = edge.kind == (last.jump_on_true
                    ? cfg::EdgeKind::BranchTrue : cfg::EdgeKind::BranchFalse);
                const auto inputs = bytecode::conditionalStackInputs(last, jump_edge);
                const auto outputs = bytecode::conditionalStackOutputs(last, jump_edge);
                for (std::uint32_t input = 0; input < inputs; ++input)
                    pop(outgoing, last.offset);
                const auto branch_value = result.value_nodes[
                    cfg.block(block_id).instruction_indices.back()];
                for (std::uint32_t output = 0; output < outputs; ++output)
                    outgoing.stack.push_back({branch_value});
            }
            if (merge(incoming[edge.target], outgoing,
                      cfg.block(block_id).start_offset,
                      cfg.block(edge.target).start_offset) && !queued[edge.target]) {
                queued[edge.target] = true;
                work.push_back(edge.target);
            }
        }
    }
    for (std::size_t i = 0; i < call_callees.size(); ++i) {
        if (visited_instructions[i] &&
            program[i].opcode == bytecode::SemanticOpcode::ImportModule)
            result.import_requests.push_back(importRequest(
                program[i], module_name_, module_is_package_, i));
        if (visited_instructions[i] && call_callees[i] != 0) {
            result.call_sites.push_back({result.value_nodes[i], caller,
                                         call_callees[i], call_groups[i]});
            result.call_instruction_indices.push_back(i);
            result.call_arguments.push_back(std::move(call_arguments[i]));
            result.call_explicit_argument_offsets.push_back(
                call_explicit_argument_offsets[i]);
            result.call_callee_inputs.push_back(
                std::move(call_callee_inputs[i]));
            result.call_results.push_back(result.value_nodes[i]);
        }
        if (!visited_instructions[i]) allocation_objects[i] = 0U;
    }
    result.allocated_objects = std::move(allocation_objects);
    return result;
}

}  // namespace cpygraph
