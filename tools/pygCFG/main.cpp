#include "api/cfg.h"
#include "common/package_cli.h"

#include <cstddef>
#include <iostream>

namespace {

constexpr char kToolName[] = "pygCFG";
constexpr char kComponentName[] = "cfg";

}  // namespace

int main(int argc, char** argv) {
    return cpygraph::tools::runPackageTool(
        argc, argv, kToolName,
        [](const cpygraph::package::PackageAnalysis& package,
           std::ostream& output) {
            // PTA/CG reachability is the prerequisite analysis phase for all
            // graph products, including a CFG-only request.
            const auto result =
                cpygraph::package::GraphBuilder().controlFlowGraphs(package);
            std::size_t blocks = 0;
            std::size_t edges = 0;
            for (const auto& code : result.graphs) {
                blocks += code.graph.blocks().size();
                edges += code.graph.edges().size();
            }
            output << "\"component\":\"" << kComponentName
                   << "\",\"modules\":" << package.moduleCount()
                   << ",\"pta_analyzed_code_objects\":"
                   << result.points_to.analyzed_code_object_count
                   << ",\"code_objects\":" << result.graphs.size()
                   << ",\"blocks\":" << blocks << ",\"edges\":" << edges;
        });
}
