#include "api/cdg.h"
#include "common/package_cli.h"

#include <cstddef>
#include <iostream>

namespace {

constexpr char kToolName[] = "pygCDG";
constexpr char kComponentName[] = "cdg";

}  // namespace

int main(int argc, char** argv) {
    return cpygraph::tools::runPackageTool(
        argc, argv, kToolName,
        [](const cpygraph::package::PackageAnalysis& package,
           std::ostream& output) {
            const auto result =
                cpygraph::package::GraphBuilder().controlDependencyGraphs(package);
            std::size_t blocks = 0U;
            std::size_t edges = 0U;
            for (const auto& code : result.graphs) {
                blocks += code.analysis.graph.blockCount();
                edges += code.analysis.graph.edges().size();
            }
            output << "\"component\":\"" << kComponentName
                   << "\",\"modules\":" << package.moduleCount()
                   << ",\"pta_analyzed_code_objects\":"
                   << result.points_to.analyzed_code_object_count
                   << ",\"code_objects\":" << result.graphs.size()
                   << ",\"blocks\":" << blocks << ",\"edges\":" << edges;
        });
}
