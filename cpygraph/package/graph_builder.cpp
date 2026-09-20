#include "package/graph_builder.h"

#include "analysis/ddg/ddg_builder.h"
#include "analysis/ddg/interprocedural.h"
#include "analysis/cfg/cfg_refiner.h"
#include "analysis/profile/profiler.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cpygraph::package {
namespace {

std::vector<std::string> parameterNames(const CodeObjectAnalysis& code) {
    const auto count = std::min(
        code.local_names.size(),
        static_cast<std::size_t>(code.argument_count) +
            static_cast<std::size_t>(code.keyword_only_argument_count) +
            static_cast<std::size_t>(code.has_var_arguments) +
            static_cast<std::size_t>(code.has_var_keywords));
    return {code.local_names.begin(), code.local_names.begin() + count};
}

bool isClassBody(const CodeObjectAnalysis& code) {
    bool stores_module = false;
    bool stores_qualname = false;
    for (const auto& instruction : code.cfg.program()) {
        if (instruction.opcode != bytecode::SemanticOpcode::StoreGlobal)
            continue;
        stores_module |= instruction.symbol == "__module__";
        stores_qualname |= instruction.symbol == "__qualname__";
    }
    return stores_module && stores_qualname;
}

struct DDGBoundaryIndex {
    std::vector<ddg::NodeId> formal_parameters;
    std::vector<ddg::NodeId> return_values;
    std::unordered_map<std::size_t, ddg::DDGCallBoundary> calls;
};

using PTAObjects = std::set<ObjectId>;
using InstructionKey = std::pair<CodeObjectId, std::size_t>;
using OffsetKey = std::pair<CodeObjectId, std::uint32_t>;

std::vector<ddg::NodeId> remapNodes(
    const std::vector<ddg::NodeId>& nodes,
    const std::vector<ddg::NodeId>& remap) {
    std::vector<ddg::NodeId> result;
    result.reserve(nodes.size());
    for (const auto node : nodes) result.push_back(remap.at(node));
    return result;
}

}  // namespace

PTAPrerequisiteSummary GraphBuilder::summarize(
    const PackageCallGraphResult& prerequisite) noexcept {
    return {prerequisite.analyzed_code_object_count,
            prerequisite.activated_callable_count,
            prerequisite.call_site_count,
            prerequisite.points_to_coverage};
}

PackageCallGraphResult GraphBuilder::callGraph(
    const PackageAnalysis& package,
    const std::vector<CodeObjectId>& entry_points,
    const PTASensitivityConfiguration& sensitivity) const {
    profile::Scope profile_scope(profile::Phase::PointsToCallGraph);
    return OnTheFlyCallGraphAnalyzer().analyze(
        package, entry_points, nullptr, sensitivity);
}

PackageControlFlowGraphResult GraphBuilder::controlFlowGraphs(
    const PackageAnalysis& package,
    const std::vector<CodeObjectId>& entry_points,
    const PTASensitivityConfiguration& sensitivity) const {
    const auto prerequisite = [&] {
        profile::Scope prerequisite_scope(profile::Phase::PointsToCallGraph);
        return OnTheFlyCallGraphAnalyzer().analyze(
            package, entry_points, nullptr, sensitivity);
    }();
    return buildControlFlowGraphs(package, prerequisite);
}

PackageControlFlowGraphResult GraphBuilder::buildControlFlowGraphs(
    const PackageAnalysis& package,
    const PackageCallGraphResult& prerequisite) {
    profile::Scope profile_scope(profile::Phase::ControlFlowGraph);
    std::vector<CodeControlFlowGraph> graphs;
    graphs.reserve(package.codeObjects().size());
    for (const auto& code : package.codeObjects()) {
        const auto parameter_count = std::min(
            code.local_names.size(),
            static_cast<std::size_t>(code.argument_count) +
                static_cast<std::size_t>(code.keyword_only_argument_count) +
                static_cast<std::size_t>(code.has_var_arguments) +
                static_cast<std::size_t>(code.has_var_keywords));
        std::vector<std::string> parameters(
            code.local_names.begin(), code.local_names.begin() + parameter_count);
        graphs.push_back({code.id,
                          cfg::CFGRefiner().refine(code.cfg, parameters)});
    }
    return {summarize(prerequisite), std::move(graphs),
            SoundnessCoverage::TypedUnresolved};
}

PackageDataDependencyGraphResult GraphBuilder::dataDependencyGraph(
    const PackageAnalysis& package,
    const std::vector<CodeObjectId>& entry_points,
    const PTASensitivityConfiguration& sensitivity) const {
    PackageAnalysisObservations observations;
    const auto prerequisite = [&] {
        profile::Scope prerequisite_scope(profile::Phase::PointsToCallGraph);
        return OnTheFlyCallGraphAnalyzer().analyze(
            package, entry_points, &observations, sensitivity);
    }();
    return buildDataDependencyGraph(
        package, prerequisite, observations, false);
}

PackageDataDependencyGraphResult GraphBuilder::dataDependencyGraph(
    const PackageAnalysis& package,
    const PackageCallGraphResult& prerequisite,
    PackageAnalysisObservations& observations) const {
    return buildDataDependencyGraph(
        package, prerequisite, observations, true);
}

PackageDataDependencyGraphResult GraphBuilder::buildDataDependencyGraph(
    const PackageAnalysis& package,
    const PackageCallGraphResult& prerequisite,
    PackageAnalysisObservations& observations,
    bool retain_observations) {
    profile::Scope profile_scope(profile::Phase::DataDependencyGraph);
    // Retain only observations needed to stitch this graph before allocating
    // DDG fragments. The diagnostic snapshot can be substantially larger.
    if (!retain_observations) {
        observations.object_origins.clear();
        observations.object_origins.shrink_to_fit();
        observations.contents.clear();
        observations.contents.shrink_to_fit();
        observations.fields.clear();
        observations.fields.shrink_to_fit();
        observations.field_names.clear();
        observations.field_names.shrink_to_fit();
        observations.attribute_field_names.clear();
        observations.attribute_field_names.shrink_to_fit();
        observations.call_sites.clear();
        observations.call_sites.shrink_to_fit();
    }
    ddg::DataDependencyGraph graph;
    std::unordered_map<CodeObjectId, DDGBoundaryIndex> boundaries;
    std::vector<PackageDataDependencyGraphResult::CallBoundary>
        call_boundaries;
    std::vector<ddg::DDGHeapAccess> heap_accesses;
    std::vector<ddg::DDGGlobalAccess> global_accesses;
    boundaries.reserve(package.codeObjects().size());
    for (const auto& code : package.codeObjects()) {
        auto fragment = ddg::DDGBuilder().buildWithBoundaries(
            code.cfg, code.id, parameterNames(code));
        const auto remap = graph.append(fragment.graph);
        DDGBoundaryIndex boundary{
            remapNodes(fragment.formal_parameters, remap),
            remapNodes(fragment.return_values, remap),
            {},
        };
        for (const auto& call : fragment.call_boundaries) {
            ddg::DDGCallBoundary remapped{
                call.instruction_index,
                remap.at(call.call),
                {},
            };
            remapped.actual_arguments.reserve(call.actual_arguments.size());
            for (const auto& argument : call.actual_arguments)
                remapped.actual_arguments.push_back(
                    remapNodes(argument, remap));
            call_boundaries.push_back({
                code.id, remapped.instruction_index, remapped.call,
                remapped.actual_arguments,
            });
            if (!boundary.calls.emplace(
                    remapped.instruction_index, std::move(remapped)).second)
                throw std::logic_error(
                    "DDG contains duplicate call instruction boundaries");
        }
        for (const auto& access : fragment.heap_accesses) {
            ddg::DDGHeapAccess remapped{
                access.instruction_index,
                access.block,
                remap.at(access.node),
                access.kind,
                access.location,
                access.field,
                remapNodes(access.bases, remap),
            };
            heap_accesses.push_back(std::move(remapped));
        }
        for (const auto& access : fragment.global_accesses)
            global_accesses.push_back({
                remap.at(access.node), access.kind, access.name});
        boundaries.emplace(code.id, std::move(boundary));
    }

    std::vector<ddg::ResolvedCallFlow> resolved_calls;
    std::vector<PackageDataDependencyGraphResult::ContextTransition>
        context_transitions;
    resolved_calls.reserve(observations.call_activations.size());
    for (const auto& activation : observations.call_activations) {
        const auto caller = boundaries.find(activation.caller);
        const auto target = boundaries.find(activation.target);
        if (caller == boundaries.end() || target == boundaries.end())
            throw std::logic_error(
                "resolved DDG call references an unknown code object");
        const auto call = caller->second.calls.find(
            activation.instruction_index);
        if (call == caller->second.calls.end())
            continue;
        ddg::ResolvedCallFlow flow;
        flow.call = call->second.call;
        for (const auto& [actual, formal] : activation.bindings) {
            if (actual >= call->second.actual_arguments.size() ||
                formal >= target->second.formal_parameters.size())
                continue;
            flow.actual_arguments.push_back(
                call->second.actual_arguments[actual]);
            flow.formal_arguments.push_back(
                target->second.formal_parameters[formal]);
            for (const auto actual_node :
                 call->second.actual_arguments[actual])
                context_transitions.push_back({
                    actual_node,
                    target->second.formal_parameters[formal],
                    activation.caller_context,
                    activation.context,
                    PackageDataDependencyGraphResult::
                        ContextTransitionKind::ActualToFormal,
                });
        }
        flow.return_values = target->second.return_values;
        for (const auto returned : target->second.return_values)
            context_transitions.push_back({
                returned,
                call->second.call,
                activation.context,
                activation.caller_context,
                PackageDataDependencyGraphResult::
                    ContextTransitionKind::ReturnToCall,
            });
        resolved_calls.push_back(std::move(flow));
    }
    std::sort(
        context_transitions.begin(), context_transitions.end(),
        [](const auto& left, const auto& right) {
            return std::tie(
                       left.source, left.target, left.source_context,
                       left.target_context, left.kind) <
                   std::tie(
                       right.source, right.target, right.source_context,
                       right.target_context, right.kind);
        });
    context_transitions.erase(
        std::unique(
            context_transitions.begin(), context_transitions.end(),
            [](const auto& left, const auto& right) {
                return left.source == right.source &&
                       left.target == right.target &&
                       left.source_context == right.source_context &&
                       left.target_context == right.target_context &&
                       left.kind == right.kind;
            }),
        context_transitions.end());
#ifndef CPYGRAPH_ABLATE_INTERPROCEDURAL_DDG
    graph = ddg::InterproceduralStitcher().stitch(
        std::move(graph), resolved_calls);
#else
    static_cast<void>(resolved_calls);
#endif

    for (const auto& load : global_accesses) {
        if (load.kind != ddg::DDGAccessKind::Load) continue;
        const auto& load_code = package.codeObject(graph.node(load.node).code);
        if (isClassBody(load_code)) continue;
        for (const auto& store : global_accesses) {
            if (store.kind != ddg::DDGAccessKind::Store ||
                store.name != load.name)
                continue;
            const auto& store_code = package.codeObject(
                graph.node(store.node).code);
            if (store_code.id != load_code.id &&
                !isClassBody(store_code) &&
                store_code.module_name == load_code.module_name)
                graph.addEdge(
                    store.node, load.node, ddg::EdgeKind::DefUse);
        }
    }

    std::map<InstructionKey, PTAObjects> objects_by_instruction;
    for (const auto& value : observations.points_to) {
        if (value.output_index != 0U) continue;
        auto& objects = objects_by_instruction[
            {value.code, value.instruction_index}];
        for (const auto object : value.objects)
            if (object != std::numeric_limits<ObjectId>::max())
                objects.insert(object);
    }
    std::map<InstructionKey, PTAObjects> heap_objects_by_instruction;
    for (const auto& value : observations.heap_bases) {
        auto& objects = heap_objects_by_instruction[
            {value.code, value.instruction_index}];
        for (const auto object : value.objects)
            if (object != std::numeric_limits<ObjectId>::max())
                objects.insert(object);
    }
    std::map<OffsetKey, std::size_t> instruction_by_offset;
    for (const auto& code : package.codeObjects())
        for (std::size_t index = 0; index < code.cfg.program().size(); ++index)
            instruction_by_offset.emplace(
                OffsetKey{code.id, code.cfg.program()[index].offset}, index);
    const auto base_objects = [&](const ddg::DDGHeapAccess& access) {
        PTAObjects result;
        const auto exact = heap_objects_by_instruction.find({
            graph.node(access.node).code, access.instruction_index});
        if (exact != heap_objects_by_instruction.end() &&
            !exact->second.empty())
            return exact->second;
        for (const auto base : access.bases) {
            const auto& node = graph.node(base);
            const auto instruction = instruction_by_offset.find(
                {node.code, node.bytecode_offset});
            if (instruction == instruction_by_offset.end()) continue;
            const auto objects = objects_by_instruction.find(
                {node.code, instruction->second});
            if (objects != objects_by_instruction.end())
                result.insert(objects->second.begin(), objects->second.end());
        }
        return result;
    };
    std::vector<PTAObjects> heap_objects;
    std::vector<std::set<ddg::NodeId>> heap_address_definitions;
    std::unordered_map<ddg::NodeId, std::set<ddg::NodeId>>
        def_use_predecessors;
    for (const auto& edge : graph.edges())
        if (edge.kind == ddg::EdgeKind::DefUse)
            def_use_predecessors[edge.target].insert(edge.source);
    heap_objects.reserve(heap_accesses.size());
    heap_address_definitions.reserve(heap_accesses.size());
    for (const auto& access : heap_accesses) {
        heap_objects.push_back(base_objects(access));
        std::set<ddg::NodeId> definitions;
        for (const auto base : access.bases) {
            const auto predecessors = def_use_predecessors.find(base);
            if (predecessors == def_use_predecessors.end())
                definitions.insert(base);
            else
                definitions.insert(predecessors->second.begin(),
                                   predecessors->second.end());
        }
        heap_address_definitions.push_back(std::move(definitions));
    }
    using HeapLocationKey =
        std::pair<ddg::DDGHeapLocationKind, std::string>;
    std::map<HeapLocationKey,
             std::unordered_map<ObjectId, std::vector<std::size_t>>>
        stores_by_location_object;
    for (std::size_t index = 0; index < heap_accesses.size(); ++index) {
        const auto& access = heap_accesses[index];
        if (access.kind != ddg::DDGAccessKind::Store) continue;
        auto& by_object = stores_by_location_object[
            {access.location, access.field}];
        for (const auto object : heap_objects[index])
            by_object[object].push_back(index);
    }
    for (std::size_t load_index = 0;
         load_index < heap_accesses.size(); ++load_index) {
        const auto& load = heap_accesses[load_index];
        if (load.kind != ddg::DDGAccessKind::Load) continue;
        std::set<std::size_t> candidate_stores;
        const auto location = stores_by_location_object.find(
            {load.location, load.field});
        if (location != stores_by_location_object.end())
            for (const auto object : heap_objects[load_index]) {
                const auto stores = location->second.find(object);
                if (stores != location->second.end())
                    candidate_stores.insert(stores->second.begin(),
                                            stores->second.end());
            }
        std::vector<std::size_t> stores;
        stores.reserve(candidate_stores.size());
        for (const auto store_index : candidate_stores) {
            const auto& store = heap_accesses[store_index];
            if (graph.node(store.node).code == graph.node(load.node).code &&
                store.instruction_index >= load.instruction_index)
                continue;
            stores.push_back(store_index);
        }
        auto latest_strong_store = heap_accesses.size();
        for (const auto store_index : stores) {
            const auto& store = heap_accesses[store_index];
            if (graph.node(store.node).code != graph.node(load.node).code ||
                store.block != load.block ||
                heap_address_definitions[store_index] !=
                    heap_address_definitions[load_index])
                continue;
            if (latest_strong_store == heap_accesses.size() ||
                store.instruction_index >
                    heap_accesses[latest_strong_store].instruction_index)
                latest_strong_store = store_index;
        }
        for (const auto store_index : stores) {
            const auto& store = heap_accesses[store_index];
            const bool killed =
                latest_strong_store != heap_accesses.size() &&
                store_index != latest_strong_store &&
                graph.node(store.node).code == graph.node(load.node).code &&
                store.block == load.block &&
                heap_address_definitions[store_index] ==
                    heap_address_definitions[load_index];
            if (killed) continue;
            // Different reaching address definitions may denote summarized
            // aliases, so they remain weak updates even in one basic block.
            graph.addEdge(
                store.node, load.node, ddg::EdgeKind::DefUse);
        }
    }
    std::sort(call_boundaries.begin(), call_boundaries.end(),
              [](const auto& left, const auto& right) {
                  return std::tie(left.code, left.instruction_index) <
                         std::tie(right.code, right.instruction_index);
              });
    return {summarize(prerequisite), std::move(graph),
            std::move(call_boundaries),
            std::move(context_transitions),
            std::move(heap_accesses),
            SoundnessCoverage::TypedUnresolved};
}

PackageControlDependencyGraphResult GraphBuilder::controlDependencyGraphs(
    const PackageAnalysis& package,
    const std::vector<CodeObjectId>& entry_points,
    const PTASensitivityConfiguration& sensitivity) const {
    const auto prerequisite = [&] {
        profile::Scope prerequisite_scope(profile::Phase::PointsToCallGraph);
        return OnTheFlyCallGraphAnalyzer().analyze(
            package, entry_points, nullptr, sensitivity);
    }();
    return buildControlDependencyGraphs(package, prerequisite);
}

PackageControlDependencyGraphResult GraphBuilder::buildControlDependencyGraphs(
    const PackageAnalysis& package,
    const PackageCallGraphResult& prerequisite,
    const PackageControlFlowGraphResult* control_flow) {
    profile::Scope profile_scope(profile::Phase::ControlDependencyGraph);
    std::vector<CodeControlDependencyGraph> graphs;
    graphs.reserve(package.codeObjects().size());
    if (control_flow != nullptr) {
        for (const auto& code_graph : control_flow->graphs)
            graphs.push_back({code_graph.code,
                              cdg::CDGBuilder().build(code_graph.graph)});
    } else {
        for (const auto& code : package.codeObjects()) {
            const auto refined = cfg::CFGRefiner().refine(
                code.cfg, parameterNames(code));
            graphs.push_back(
                {code.id, cdg::CDGBuilder().build(refined)});
        }
    }
    return {summarize(prerequisite), std::move(graphs),
            SoundnessCoverage::TypedUnresolved};
}

PackageGraphAnalysisResult GraphBuilder::analyze(
    const PackageAnalysis& package,
    const std::vector<CodeObjectId>& entry_points,
    const PTASensitivityConfiguration& sensitivity,
    PackageAnalysisObservations* retained_observations) const {
    PackageAnalysisObservations observations;
    auto call_graph = [&] {
        profile::Scope prerequisite_scope(profile::Phase::PointsToCallGraph);
        return OnTheFlyCallGraphAnalyzer().analyze(
            package, entry_points, &observations, sensitivity);
    }();
    auto control_flow = buildControlFlowGraphs(package, call_graph);
    auto control_dependency = buildControlDependencyGraphs(
        package, call_graph, &control_flow);
    auto data_dependency = buildDataDependencyGraph(
        package, call_graph, observations, retained_observations != nullptr);
    if (retained_observations != nullptr)
        *retained_observations = std::move(observations);
    return {std::move(call_graph), std::move(control_flow),
            std::move(control_dependency), std::move(data_dependency)};
}

}  // namespace cpygraph::package
