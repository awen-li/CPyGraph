#include "api/package.h"
#include "api/visualization.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kUsageExitCode = 2;
constexpr int kAnalysisFailureExitCode = 1;
constexpr char kPackageInitializer[] = "__init__.py";
constexpr std::string_view kDumpOption = "--dump-observations";
constexpr std::string_view kSensitivityOption = "--sensitivity";
constexpr std::string_view kDotDirectoryOption = "--dot-directory";
constexpr std::string_view kInsensitivePolicy = "insensitive";
constexpr std::string_view kSelectiveFlowPolicy = "selective-flow";
constexpr std::string_view kSelectiveContextPolicy = "selective-context";
constexpr std::string_view kSelectivePathPolicy = "selective-path";
constexpr std::string_view kSelectiveAllPolicy = "selective-all";
constexpr std::string_view kSelectiveEntryPolicy = "selective-entry";
constexpr std::string_view kCompletePolicy = "complete";
constexpr auto kAllSensitivities = cpygraph::PTASensitivity::Flow |
                                   cpygraph::PTASensitivity::Context |
                                   cpygraph::PTASensitivity::Path;

enum class AnalysisPolicy {
    Insensitive,
    SelectiveFlow,
    SelectiveContext,
    SelectivePath,
    SelectiveAll,
    SelectiveEntry,
    Complete,
};

AnalysisPolicy parsePolicy(std::string_view value) {
    if (value == kInsensitivePolicy) return AnalysisPolicy::Insensitive;
    if (value == kSelectiveFlowPolicy) return AnalysisPolicy::SelectiveFlow;
    if (value == kSelectiveContextPolicy)
        return AnalysisPolicy::SelectiveContext;
    if (value == kSelectivePathPolicy) return AnalysisPolicy::SelectivePath;
    if (value == kSelectiveAllPolicy) return AnalysisPolicy::SelectiveAll;
    if (value == kSelectiveEntryPolicy) return AnalysisPolicy::SelectiveEntry;
    if (value == kCompletePolicy) return AnalysisPolicy::Complete;
    throw std::invalid_argument("unknown sensitivity policy: " +
                                std::string(value));
}

std::string_view policyName(AnalysisPolicy policy) noexcept {
    switch (policy) {
        case AnalysisPolicy::Insensitive: return kInsensitivePolicy;
        case AnalysisPolicy::SelectiveFlow: return kSelectiveFlowPolicy;
        case AnalysisPolicy::SelectiveContext: return kSelectiveContextPolicy;
        case AnalysisPolicy::SelectivePath: return kSelectivePathPolicy;
        case AnalysisPolicy::SelectiveAll: return kSelectiveAllPolicy;
        case AnalysisPolicy::SelectiveEntry: return kSelectiveEntryPolicy;
        case AnalysisPolicy::Complete: return kCompletePolicy;
    }
    return kInsensitivePolicy;
}

cpygraph::PTASensitivityConfiguration sensitivityConfiguration(
    const cpygraph::package::PackageAnalysis& package,
    const std::vector<cpygraph::CodeObjectId>& entry_points,
    AnalysisPolicy policy) {
    cpygraph::PTASensitivityConfiguration result;
    if (policy == AnalysisPolicy::Insensitive) return result;
    if (policy == AnalysisPolicy::Complete) {
        result.level = cpygraph::PTASensitivityLevel::Complete;
        return result;
    }
    auto attributes = kAllSensitivities;
    if (policy == AnalysisPolicy::SelectiveFlow)
        attributes = cpygraph::PTASensitivity::Flow;
    else if (policy == AnalysisPolicy::SelectiveContext)
        attributes = cpygraph::PTASensitivity::Context;
    else if (policy == AnalysisPolicy::SelectivePath)
        attributes = cpygraph::PTASensitivity::Path;
    result.level = cpygraph::PTASensitivityLevel::Selective;
    if (policy == AnalysisPolicy::SelectiveEntry) {
        result.functions.reserve(entry_points.size());
        for (const auto code : entry_points)
            result.functions.push_back({code, kAllSensitivities});
        return result;
    }
    result.functions.reserve(package.codeObjects().size());
    for (const auto& code : package.codeObjects())
        result.functions.push_back({code.id, attributes});
    return result;
}

void jsonString(std::ostream& output, std::string_view value) {
    output << '"';
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (byte < 0x20U) {
                    const auto flags = output.flags();
                    const auto fill = output.fill();
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(byte);
                    output.flags(flags);
                    output.fill(fill);
                } else {
                    output << character;
                }
        }
    }
    output << '"';
}

std::filesystem::path analysisInput(const std::filesystem::path& case_path) {
    if (case_path.filename() == kPackageInitializer) return case_path.parent_path();
    return case_path;
}

std::vector<cpygraph::CodeObjectId> resolveEntryPoints(
    const cpygraph::package::PackageAnalysis& package,
    const std::filesystem::path& case_path,
    const std::vector<std::string_view>& entry_names) {
    constexpr std::string_view kModuleEntry = "<module>";
    const auto source = std::filesystem::weakly_canonical(case_path);
    std::vector<cpygraph::CodeObjectId> result;
    for (const auto name : entry_names) {
        if (name == kModuleEntry) continue;
        cpygraph::CodeObjectId found{};
        for (const auto& code : package.codeObjects()) {
            if (code.name != name || code.parent == 0U ||
                std::filesystem::weakly_canonical(code.filename) != source)
                continue;
            const auto& parent = package.codeObject(code.parent);
            if (parent.parent != 0U) continue;
            if (found != 0U)
                throw std::runtime_error("entry-point name is ambiguous: " +
                                         std::string(name));
            found = code.id;
        }
        if (found == 0U)
            throw std::runtime_error("entry point does not exist: " +
                                     std::string(name));
        result.push_back(found);
    }
    return result;
}

int analyze(const std::filesystem::path& case_path,
            const std::vector<std::string_view>& entry_names,
            bool dump_observations,
            AnalysisPolicy policy,
            const std::filesystem::path& dot_directory) {
    const auto compiled = cpygraph::package::PackageCompiler().compile(
        analysisInput(case_path));
    const auto loaded = cpygraph::package::PackageLoader().load(compiled);
    const auto package = cpygraph::package::PackageAnalyzer().analyze(loaded);
    const auto entry_points = resolveEntryPoints(package, case_path, entry_names);
    const auto sensitivity = sensitivityConfiguration(
        package, entry_points, policy);
    const cpygraph::package::GraphBuilder builder;

    cpygraph::package::PackageAnalysisObservations observations;
    const auto result = builder.analyze(
        package, entry_points, sensitivity,
        dump_observations ? &observations : nullptr);
    const auto& call_graph = result.call_graph;
    const auto& control_flow = result.control_flow;
    const auto& control_dependency = result.control_dependency;
    const auto& data_dependency = result.data_dependency;

    if (package.moduleCount() == 0U || package.codeObjects().empty())
        throw std::runtime_error("case produced no analyzable code objects");
    if (control_flow.graphs.size() != package.codeObjects().size())
        throw std::runtime_error("CFG coverage does not match loaded code objects");
    if (control_dependency.graphs.size() != package.codeObjects().size())
        throw std::runtime_error("CDG coverage does not match loaded code objects");
    if (call_graph.analyzed_code_object_count == 0U ||
        control_flow.points_to.analyzed_code_object_count == 0U ||
        control_dependency.points_to.analyzed_code_object_count == 0U ||
        data_dependency.points_to.analyzed_code_object_count == 0U)
        throw std::runtime_error("on-the-fly PTA prerequisite did not run");

    if (!dot_directory.empty()) {
        std::filesystem::create_directories(dot_directory);
        cpygraph::visualization::CallGraphDotGenerator(call_graph.graph).write(
            dot_directory / "cg.dot");
        for (const auto& code_graph : control_flow.graphs)
            cpygraph::visualization::CFGDotGenerator(code_graph.graph).write(
                dot_directory /
                ("cfg-" + std::to_string(code_graph.code) + ".dot"));
        for (const auto& code_graph : control_dependency.graphs)
            cpygraph::visualization::CDGDotGenerator(
                code_graph.analysis.graph).write(
                    dot_directory /
                    ("cdg-" + std::to_string(code_graph.code) + ".dot"));
        cpygraph::visualization::DDGDotGenerator(data_dependency.graph).write(
            dot_directory / "ddg.dot");
    }

    std::cout << "{\"status\":\"PASS\""
              << ",\"verdict_scope\":\"pipeline\""
              << ",\"pta\":\"PASS\""
              << ",\"cg\":\"PASS\""
              << ",\"cfg\":\"PASS\""
              << ",\"cdg\":\"PASS\""
              << ",\"ddg\":\"PASS\""
              << ",\"analysis_policy\":";
    jsonString(std::cout, policyName(policy));
    std::cout
              << ",\"modules\":" << package.moduleCount()
              << ",\"code_objects\":" << package.codeObjects().size()
              << ",\"entry_points\":" << entry_points.size()
              << ",\"sensitive_functions\":"
              << (policy == AnalysisPolicy::Complete
                      ? package.codeObjects().size()
                      : sensitivity.functions.size())
              << ",\"cg_edges\":" << call_graph.graph.edges().size()
              << ",\"cfg_graphs\":" << control_flow.graphs.size()
              << ",\"cdg_graphs\":" << control_dependency.graphs.size()
              << ",\"ddg_nodes\":" << data_dependency.graph.nodes().size();
    if (dump_observations) {
        std::cout << ",\"python_version\":";
        jsonString(std::cout, package.pythonVersion());
        std::cout << ",\"soundness\":{\"pta\":"
                  << static_cast<unsigned int>(call_graph.points_to_coverage)
                  << ",\"cg\":"
                  << static_cast<unsigned int>(call_graph.call_graph_coverage)
                  << ",\"cfg\":"
                  << static_cast<unsigned int>(control_flow.coverage)
                  << ",\"cdg\":"
                  << static_cast<unsigned int>(control_dependency.coverage)
                  << ",\"ddg\":"
                  << static_cast<unsigned int>(data_dependency.coverage) << '}';
        std::cout << ",\"code_objects\":[";
        bool first = true;
        for (const auto& code : package.codeObjects()) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"id\":" << code.id << ",\"parent\":" << code.parent
                      << ",\"module\":";
            jsonString(std::cout, code.module_name);
            std::cout << ",\"name\":";
            jsonString(std::cout, code.name);
            std::cout << ",\"qualname\":";
            jsonString(std::cout, code.qualname);
            std::cout << ",\"filename\":";
            jsonString(std::cout, code.filename.string());
            std::cout << ",\"argument_count\":" << code.argument_count
                      << ",\"keyword_only_argument_count\":"
                      << code.keyword_only_argument_count
                      << ",\"locals\":[";
            for (std::size_t index = 0; index < code.local_names.size(); ++index) {
                if (index != 0U) std::cout << ',';
                jsonString(std::cout, code.local_names[index]);
            }
            std::cout << ']';
            std::cout << ",\"instructions\":[";
            bool first_instruction = true;
            const auto& program = code.cfg.program();
            for (std::size_t index = 0; index < program.size(); ++index) {
                const auto& instruction = program[index];
                if (!first_instruction) std::cout << ',';
                first_instruction = false;
                std::cout << "{\"index\":" << index
                          << ",\"offset\":" << instruction.offset
                          << ",\"opcode\":" << static_cast<unsigned int>(instruction.opcode)
                          << ",\"symbol\":";
                jsonString(std::cout, instruction.symbol);
                std::cout << ",\"secondary_symbol\":";
                jsonString(std::cout, instruction.secondary_symbol);
                std::cout << ",\"protocol\":"
                          << static_cast<unsigned int>(instruction.protocol_operation)
                          << ",\"protocol_methods\":"
                          << instruction.protocol_methods.encoded << '}';
            }
            std::cout << "]}";
        }
        std::cout << "],\"pta\":{\"values\":[";
        first = true;
        for (const auto& value : observations.points_to) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"code\":" << value.code
                      << ",\"instruction\":" << value.instruction_index
                      << ",\"value\":" << value.value
                      << ",\"context\":" << value.context
                      << ",\"output_index\":"
                      << static_cast<unsigned int>(value.output_index)
                      << ",\"objects\":[";
            for (std::size_t index = 0; index < value.objects.size(); ++index) {
                if (index != 0U) std::cout << ',';
                std::cout << value.objects[index];
            }
            std::cout << "]}";
        }
        std::cout << "],\"origins\":[";
        first = true;
        for (const auto& origin : observations.object_origins) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"object\":" << origin.object
                      << ",\"kind\":" << static_cast<unsigned int>(origin.kind)
                      << ",\"code\":" << origin.code
                      << ",\"index\":" << origin.index
                      << ",\"context\":" << origin.context << '}';
        }
        std::cout << "],\"contents\":[";
        first = true;
        for (const auto& fact : observations.contents) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"base\":" << fact.base
                      << ",\"object\":" << fact.object << '}';
        }
        std::cout << "],\"fields\":[";
        first = true;
        for (const auto& fact : observations.fields) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"base\":" << fact.base << ",\"field\":" << fact.field
                      << ",\"object\":" << fact.object << '}';
        }
        std::cout << "],\"field_names\":[";
        first = true;
        for (const auto& field : observations.field_names) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"field\":" << field.field << ",\"name\":";
            jsonString(std::cout, field.name);
            std::cout << '}';
        }
        std::cout << "]},\"cg\":{\"sites\":[";
        first = true;
        for (const auto& site : observations.call_sites) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"site\":" << site.site << ",\"code\":" << site.code
                      << ",\"instruction\":" << site.instruction_index
                      << ",\"context\":" << site.context
                      << ",\"callee_inputs\":[";
            for (std::size_t index = 0; index < site.callee_inputs.size();
                 ++index) {
                if (index != 0U) std::cout << ',';
                std::cout << site.callee_inputs[index];
            }
            std::cout << "]}";
        }
        std::cout << "],\"activations\":[";
        first = true;
        for (const auto& activation : observations.call_activations) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"site\":" << activation.site
                      << ",\"target\":" << activation.target
                      << ",\"context\":" << activation.context << '}';
        }
        std::cout << "],\"edges\":[";
        first = true;
        for (const auto& edge : call_graph.graph.edges()) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"site\":" << edge.site << ",\"caller\":" << edge.caller;
            if (edge.target) {
                std::cout << ",\"target\":" << edge.target->code
                          << ",\"unresolved\":null";
            } else {
                const auto& group = call_graph.graph.unresolvedGroup(edge.unresolved_group);
                std::cout << ",\"target\":null,\"unresolved\":{\"kind\":"
                          << static_cast<unsigned int>(group.kind)
                          << ",\"operation\":"
                          << static_cast<unsigned int>(group.operation)
                          << ",\"methods\":"
                          << group.candidate_methods.encoded << '}';
            }
            std::cout << '}';
        }
        std::cout << "]},\"cfg\":[";
        first = true;
        for (const auto& code_graph : control_flow.graphs) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"code\":" << code_graph.code << ",\"blocks\":[";
            bool first_block = true;
            for (const auto& block : code_graph.graph.blocks()) {
                if (!first_block) std::cout << ',';
                first_block = false;
                std::cout << "{\"id\":" << block.id
                          << ",\"start_offset\":" << block.start_offset
                          << ",\"instructions\":[";
                for (std::size_t index = 0; index < block.instruction_indices.size(); ++index) {
                    if (index != 0U) std::cout << ',';
                    std::cout << block.instruction_indices[index];
                }
                std::cout << "]}";
            }
            std::cout << "],\"edges\":[";
            bool first_edge = true;
            for (const auto& edge : code_graph.graph.edges()) {
                if (!first_edge) std::cout << ',';
                first_edge = false;
                std::cout << "{\"source\":" << edge.source
                          << ",\"target\":" << edge.target
                          << ",\"kind\":" << static_cast<unsigned int>(edge.kind) << '}';
            }
            std::cout << "]}";
        }
        std::cout << "],\"cdg\":[";
        first = true;
        for (const auto& code_graph : control_dependency.graphs) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"code\":" << code_graph.code
                      << ",\"block_count\":"
                      << code_graph.analysis.graph.blockCount()
                      << ",\"postdominators\":[";
            bool first_block = true;
            for (std::size_t index = 0;
                 index < code_graph.analysis.postdominators.blockCount();
                 ++index) {
                if (!first_block) std::cout << ',';
                first_block = false;
                const auto block = static_cast<cpygraph::cfg::BlockId>(index + 1U);
                std::cout << "{\"block\":" << block
                          << ",\"immediate\":"
                          << code_graph.analysis.postdominators
                                 .immediatePostDominator(block)
                          << ",\"set\":[";
                const auto values = code_graph.analysis.postdominators
                                        .postdominators(block);
                for (std::size_t value_index = 0;
                     value_index < values.size(); ++value_index) {
                    if (value_index != 0U) std::cout << ',';
                    std::cout << values[value_index];
                }
                std::cout << "]}";
            }
            std::cout << "],\"edges\":[";
            bool first_edge = true;
            for (const auto& edge : code_graph.analysis.graph.edges()) {
                if (!first_edge) std::cout << ',';
                first_edge = false;
                std::cout << "{\"controller\":" << edge.controller
                          << ",\"dependent\":" << edge.dependent
                          << ",\"outcome\":"
                          << static_cast<unsigned int>(edge.outcome) << '}';
            }
            std::cout << "]}";
        }
        std::cout << "],\"ddg\":{\"nodes\":[";
        first = true;
        for (const auto& node : data_dependency.graph.nodes()) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"id\":" << node.id << ",\"code\":" << node.code
                      << ",\"offset\":" << node.bytecode_offset
                      << ",\"kind\":" << static_cast<unsigned int>(node.kind)
                      << ",\"label\":";
            jsonString(std::cout, data_dependency.graph.debugString(node.label));
            std::cout << '}';
        }
        std::cout << "],\"edges\":[";
        first = true;
        for (const auto& edge : data_dependency.graph.edges()) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"source\":" << edge.source
                      << ",\"target\":" << edge.target
                      << ",\"kind\":" << static_cast<unsigned int>(edge.kind) << '}';
        }
        std::cout << "]}";
    }
    std::cout << "}\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: pygbench_case_analysis [--dump-observations] "
                     "[--sensitivity insensitive|selective-flow|"
                     "selective-context|selective-path|selective-all|"
                     "selective-entry|complete] "
                     "[--dot-directory DIR] "
                     "<case.py|package/__init__.py> [entry ...]\n";
        return kUsageExitCode;
    }
    try {
        int path_index = 1;
        bool dump_observations = false;
        AnalysisPolicy policy = AnalysisPolicy::Insensitive;
        std::filesystem::path dot_directory;
        while (path_index < argc) {
            const std::string_view option(argv[path_index]);
            if (option == kDumpOption) {
                dump_observations = true;
                ++path_index;
                continue;
            }
            if (option == kSensitivityOption) {
                if (++path_index >= argc)
                    throw std::invalid_argument(
                        "sensitivity policy is missing");
                policy = parsePolicy(argv[path_index]);
                ++path_index;
                continue;
            }
            if (option == kDotDirectoryOption) {
                if (++path_index >= argc)
                    throw std::invalid_argument(
                        "DOT output directory is missing");
                dot_directory = argv[path_index];
                ++path_index;
                continue;
            }
            break;
        }
        if (path_index >= argc) throw std::invalid_argument("case path is missing");
        std::vector<std::string_view> entry_names;
        entry_names.reserve(static_cast<std::size_t>(argc - path_index - 1));
        for (int index = path_index + 1; index < argc; ++index)
            entry_names.emplace_back(argv[index]);
        return analyze(argv[path_index], entry_names, dump_observations, policy,
                       dot_directory);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return kAnalysisFailureExitCode;
    }
}
