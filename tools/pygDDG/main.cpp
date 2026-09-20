#include "api/ddg.h"
#include "common/package_cli.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <numeric>

namespace {

constexpr char kToolName[] = "pygDDG";
constexpr char kComponentName[] = "ddg";
constexpr std::size_t kAnalysisSchemaVersion = 2U;

}  // namespace

int main(int argc, char** argv) {
    return cpygraph::tools::runPackageTool(
        argc, argv, kToolName,
        [](const cpygraph::package::PackageAnalysis& package,
           std::ostream& output) {
            const auto result =
                cpygraph::package::GraphBuilder().analyze(package);
            const auto instruction_count = std::accumulate(
                package.codeObjects().begin(), package.codeObjects().end(),
                std::size_t{}, [](std::size_t count, const auto& code) {
                    return count + code.cfg.program().size();
                });
            const auto cfg_block_count = std::accumulate(
                result.control_flow.graphs.begin(),
                result.control_flow.graphs.end(), std::size_t{},
                [](std::size_t count, const auto& code) {
                    return count + code.graph.blocks().size();
                });
            const auto cfg_edge_count = std::accumulate(
                result.control_flow.graphs.begin(),
                result.control_flow.graphs.end(), std::size_t{},
                [](std::size_t count, const auto& code) {
                    return count + code.graph.edges().size();
                });
            const auto cdg_block_count = std::accumulate(
                result.control_dependency.graphs.begin(),
                result.control_dependency.graphs.end(), std::size_t{},
                [](std::size_t count, const auto& code) {
                    return count + code.analysis.graph.blockCount();
                });
            const auto cdg_edge_count = std::accumulate(
                result.control_dependency.graphs.begin(),
                result.control_dependency.graphs.end(), std::size_t{},
                [](std::size_t count, const auto& code) {
                    return count + code.analysis.graph.edges().size();
                });
            const auto& call_graph = result.call_graph;
            const auto resolved_call_targets = static_cast<std::size_t>(
                std::count_if(
                    call_graph.graph.edges().begin(),
                    call_graph.graph.edges().end(),
                    [](const auto& edge) { return edge.target.has_value(); }));
            const auto unresolved_call_sites = static_cast<std::size_t>(
                std::count_if(
                    call_graph.graph.edges().begin(),
                    call_graph.graph.edges().end(),
                    [](const auto& edge) { return !edge.target.has_value(); }));
            const auto& pta = call_graph.points_to_statistics;
            output << "\"component\":\"" << kComponentName
                   << "\",\"analysis_schema_version\":"
                   << kAnalysisSchemaVersion
                   << ",\"python_version\":\"" << package.pythonVersion()
                   << "\",\"modules\":" << package.moduleCount()
                   << ",\"code_objects\":" << package.codeObjects().size()
                   << ",\"native_instructions\":" << instruction_count
                   << ",\"pta\":{\"analyzed_code_objects\":"
                   << call_graph.analyzed_code_object_count
                   << ",\"activated_callables\":"
                   << call_graph.activated_callable_count
                   << ",\"values\":" << pta.value_count
                   << ",\"objects\":" << pta.object_count
                   << ",\"constraints\":" << pta.constraint_count
                   << ",\"points_to_facts\":" << pta.points_to_fact_count
                   << ",\"content_facts\":" << pta.content_fact_count
                   << ",\"field_facts\":" << pta.field_fact_count
                   << ",\"solver_iterations\":"
                   << pta.solver_iteration_count << '}'
                   << ",\"cg\":{\"nodes\":"
                   << call_graph.graph.nodes().size()
                   << ",\"edges\":" << call_graph.graph.edges().size()
                   << ",\"call_sites\":" << call_graph.call_site_count
                   << ",\"resolved_targets\":" << resolved_call_targets
                   << ",\"unresolved_call_sites\":"
                   << unresolved_call_sites
                   << ",\"unresolved_groups\":"
                   << call_graph.graph.unresolvedGroupCount() << '}'
                   << ",\"cfg\":{\"graphs\":"
                   << result.control_flow.graphs.size()
                   << ",\"blocks\":" << cfg_block_count
                   << ",\"edges\":" << cfg_edge_count << '}'
                   << ",\"cdg\":{\"graphs\":"
                   << result.control_dependency.graphs.size()
                   << ",\"blocks\":" << cdg_block_count
                   << ",\"edges\":" << cdg_edge_count << '}'
                   << ",\"ddg\":{\"nodes\":"
                   << result.data_dependency.graph.nodes().size()
                   << ",\"edges\":"
                   << result.data_dependency.graph.edges().size() << '}';
        });
}
