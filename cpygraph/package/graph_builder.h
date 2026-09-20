#pragma once

#include "analysis/cdg/cdg_builder.h"
#include "analysis/cfg/control_flow_graph.h"
#include "analysis/ddg/ddg_builder.h"
#include "package/call_graph.h"

#include <cstddef>
#include <vector>

namespace cpygraph::package {

struct PTAPrerequisiteSummary {
    std::size_t analyzed_code_object_count{};
    std::size_t activated_callable_count{};
    std::size_t call_site_count{};
    SoundnessCoverage coverage{SoundnessCoverage::ConservativeTop};
};

struct CodeControlFlowGraph {
    CodeObjectId code{};
    cfg::ControlFlowGraph graph;
};

struct PackageControlFlowGraphResult {
    PTAPrerequisiteSummary points_to;
    std::vector<CodeControlFlowGraph> graphs;
    SoundnessCoverage coverage{SoundnessCoverage::TypedUnresolved};
};

struct PackageDataDependencyGraphResult {
    PTAPrerequisiteSummary points_to;
    ddg::DataDependencyGraph graph;
    // Exact package-level call boundaries use the same remapped node IDs as
    // graph. Clients can therefore select source-level arguments without
    // reconstructing DDG fragments or treating every call operand alike.
    struct CallBoundary {
        CodeObjectId code{};
        std::size_t instruction_index{};
        ddg::NodeId call{};
        std::vector<std::vector<ddg::NodeId>> actual_arguments;
    };
    std::vector<CallBoundary> call_boundaries;
    enum class ContextTransitionKind : std::uint8_t {
        ActualToFormal,
        ReturnToCall,
    };
    // Exact context changes introduced by interprocedural DDG stitching.
    // Intraprocedural edges retain their current context.  Consumers can use
    // this relation to avoid joining one caller context to another merely
    // because the package graph shares lexical DDG nodes across contexts.
    struct ContextTransition {
        ddg::NodeId source{};
        ddg::NodeId target{};
        PTAContextId source_context{};
        PTAContextId target_context{};
        ContextTransitionKind kind{ContextTransitionKind::ActualToFormal};
    };
    std::vector<ContextTransition> context_transitions;
    // Remapped package-level heap boundaries use node IDs from graph. Clients
    // can anchor receiver-sensitive model facts without rebuilding a DDG
    // fragment or parsing bytecode stack behavior.
    std::vector<ddg::DDGHeapAccess> heap_accesses;
    SoundnessCoverage coverage{SoundnessCoverage::ConservativeTop};
};

struct CodeControlDependencyGraph {
    CodeObjectId code{};
    cdg::ControlDependenceAnalysis analysis;
};

struct PackageControlDependencyGraphResult {
    PTAPrerequisiteSummary points_to;
    std::vector<CodeControlDependencyGraph> graphs;
    SoundnessCoverage coverage{SoundnessCoverage::TypedUnresolved};
};

// One coupled package run for clients that consume several graph products.
// PTA/CG is solved exactly once; CFG, CDG, and DDG are then materialized from
// the same package and points-to observations.
struct PackageGraphAnalysisResult {
    PackageCallGraphResult call_graph;
    PackageControlFlowGraphResult control_flow;
    PackageControlDependencyGraphResult control_dependency;
    PackageDataDependencyGraphResult data_dependency;
};

// High-level graph construction API. Every graph product completes the
// on-the-fly points-to prerequisite before its result is materialized.
class GraphBuilder {
public:
    PackageCallGraphResult callGraph(
        const PackageAnalysis& package,
        const std::vector<CodeObjectId>& entry_points = {},
        const PTASensitivityConfiguration& sensitivity = {}) const;
    PackageControlFlowGraphResult controlFlowGraphs(
        const PackageAnalysis& package,
        const std::vector<CodeObjectId>& entry_points = {},
        const PTASensitivityConfiguration& sensitivity = {}) const;
    PackageDataDependencyGraphResult dataDependencyGraph(
        const PackageAnalysis& package,
        const std::vector<CodeObjectId>& entry_points = {},
        const PTASensitivityConfiguration& sensitivity = {}) const;
    // Materializes only the DDG from a compatible, already-computed PTA/CG
    // fixed point. The supplied observation snapshot is preserved.
    PackageDataDependencyGraphResult dataDependencyGraph(
        const PackageAnalysis& package,
        const PackageCallGraphResult& prerequisite,
        PackageAnalysisObservations& observations) const;
    PackageControlDependencyGraphResult controlDependencyGraphs(
        const PackageAnalysis& package,
        const std::vector<CodeObjectId>& entry_points = {},
        const PTASensitivityConfiguration& sensitivity = {}) const;
    PackageGraphAnalysisResult analyze(
        const PackageAnalysis& package,
        const std::vector<CodeObjectId>& entry_points = {},
        const PTASensitivityConfiguration& sensitivity = {},
        PackageAnalysisObservations* observations = nullptr) const;

private:
    static PTAPrerequisiteSummary summarize(
        const PackageCallGraphResult& prerequisite) noexcept;
    static PackageControlFlowGraphResult buildControlFlowGraphs(
        const PackageAnalysis& package,
        const PackageCallGraphResult& prerequisite);
    static PackageControlDependencyGraphResult buildControlDependencyGraphs(
        const PackageAnalysis& package,
        const PackageCallGraphResult& prerequisite,
        const PackageControlFlowGraphResult* control_flow = nullptr);
    static PackageDataDependencyGraphResult buildDataDependencyGraph(
        const PackageAnalysis& package,
        const PackageCallGraphResult& prerequisite,
        PackageAnalysisObservations& observations,
        bool retain_observations);
};

}  // namespace cpygraph::package
