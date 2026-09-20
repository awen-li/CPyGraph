#include "api/cdg.h"
#include "api/cfg.h"
#include "api/visualization.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace {

constexpr std::uint32_t kFirstOffset = 0;
constexpr std::uint32_t kSecondOffset = 2;
constexpr std::uint32_t kThirdOffset = 4;
constexpr cpygraph::CodeObjectId kCallerCode = 11;
constexpr cpygraph::CodeObjectId kCalleeCode = 12;
constexpr cpygraph::FunctionId kCalleeFunction = 21;
constexpr std::uint64_t kResolvedSite = 31;
constexpr std::uint64_t kUnresolvedSite = 32;
constexpr char kEscapedLabel[] = "quoted \"value\"\nnext";
constexpr char kRectangleAttribute[] = "shape=box";
constexpr char kForbiddenEllipse[] = "shape=ellipse";
constexpr char kForbiddenDiamond[] = "shape=diamond";
constexpr char kOutputFilename[] = "cpygraph_visualization_test.dot";

}  // namespace

int main() {
    using cpygraph::bytecode::SemanticOpcode;
    const auto cfg = cpygraph::cfg::CFGBuilder().build({
        {kFirstOffset, SemanticOpcode::Nop},
        {kSecondOffset, SemanticOpcode::Return, {}, {}, {}, {}, {}, {}, {}, true},
    });
    const cpygraph::visualization::CFGDotGenerator cfg_generator(cfg);
    const auto cfg_dot = cfg_generator.generate();
    require(cfg_dot.find("digraph \"cfg\"") != std::string::npos,
            "CFG visualizer emits a named directed graph");
    require(cfg_dot.find(kRectangleAttribute) != std::string::npos,
            "CFG visualizer emits rectangular nodes");

    const auto cdg = cpygraph::cdg::CDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build({
            {kFirstOffset, SemanticOpcode::ConditionalBranch, {}, {},
             kThirdOffset, true},
            {kSecondOffset, SemanticOpcode::Return, {}, {}, {}, {}, {}, {}, {}, true},
            {kThirdOffset, SemanticOpcode::Return, {}, {}, {}, {}, {}, {}, {}, true},
        }));
    const auto cdg_dot =
        cpygraph::visualization::CDGDotGenerator(cdg.graph).generate();
    require(cdg_dot.find("digraph \"cdg\"") != std::string::npos &&
                cdg_dot.find(kRectangleAttribute) != std::string::npos,
            "CDG visualizer emits a named graph with rectangular nodes");
    require(cdg_dot.find("true") != std::string::npos &&
                cdg_dot.find("false") != std::string::npos,
            "CDG visualizer retains branch outcome labels");

    const cpygraph::cg::CallGraph call_graph({
        {kResolvedSite, kCallerCode,
         cpygraph::cg::CallableTarget{kCalleeFunction, kCalleeCode}},
        {kUnresolvedSite, kCallerCode, std::nullopt},
    });
    const auto call_dot =
        cpygraph::visualization::CallGraphDotGenerator(call_graph).generate();
    require(call_dot.find("function 21") != std::string::npos,
            "call-graph visualizer labels resolved function edges");
    require(call_dot.find("python.dynamic_call") != std::string::npos,
            "call-graph visualizer labels unresolved target groups");
    require(call_dot.find(kForbiddenDiamond) == std::string::npos,
            "unresolved call targets remain rectangular");

    cpygraph::ddg::DataDependencyGraph ddg;
    const auto source = ddg.addNode(cpygraph::ddg::NodeKind::Input,
                                    kEscapedLabel, kFirstOffset, kCallerCode);
    const auto target = ddg.addNode(cpygraph::ddg::NodeKind::Return,
                                    "return", kSecondOffset, kCallerCode);
    ddg.addEdge(source, target, cpygraph::ddg::EdgeKind::ReturnValue);
    const cpygraph::visualization::DDGDotGenerator ddg_generator(ddg);
    const cpygraph::visualization::DotGenerator& base = ddg_generator;
    const auto ddg_dot = base.generate();
    require(ddg_dot.find("quoted \\\"value\\\"\\nnext") != std::string::npos,
            "base DOT generator escapes graph labels");
    require(ddg_dot.find(kRectangleAttribute) != std::string::npos &&
                ddg_dot.find(kForbiddenEllipse) == std::string::npos,
            "DDG visualizer emits rectangular nodes");
    require(ddg_dot.find("return_value") != std::string::npos,
            "DDG visualizer labels typed edges");

    const auto output_path = std::filesystem::current_path() / kOutputFilename;
    base.write(output_path);
    std::ifstream input(output_path, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    require(written == ddg_dot, "base DOT generator writes the generated document");
    input.close();
    std::filesystem::remove(output_path);
}
