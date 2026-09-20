#include "visualization/dot_generator.h"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace cpygraph::visualization {
namespace {

constexpr char kCFGGraphName[] = "cfg";
constexpr char kCDGGraphName[] = "cdg";
constexpr char kCallGraphName[] = "call_graph";
constexpr char kDDGGraphName[] = "ddg";

std::string blockId(cfg::BlockId id) {
    return "block_" + std::to_string(id);
}

std::string codeId(CodeObjectId id) {
    return "code_" + std::to_string(id);
}

std::string unknownTargetId(std::uint64_t site) {
    return "unknown_site_" + std::to_string(site);
}

std::string dataNodeId(ddg::NodeId id) {
    return "data_" + std::to_string(id);
}

std::string_view cfgEdgeName(cfg::EdgeKind kind) noexcept {
    switch (kind) {
        case cfg::EdgeKind::Fallthrough: return "fallthrough";
        case cfg::EdgeKind::BranchTrue: return "branch_true";
        case cfg::EdgeKind::BranchFalse: return "branch_false";
        case cfg::EdgeKind::Jump: return "jump";
        case cfg::EdgeKind::Exception: return "exception";
        case cfg::EdgeKind::Resume: return "resume";
    }
    return "unknown";
}

std::string_view branchOutcomeName(cdg::BranchOutcome outcome) noexcept {
    switch (outcome) {
        case cdg::BranchOutcome::Normal: return "normal";
        case cdg::BranchOutcome::True: return "true";
        case cdg::BranchOutcome::False: return "false";
        case cdg::BranchOutcome::Exception: return "exception";
        case cdg::BranchOutcome::Resume: return "resume";
    }
    return "unknown";
}

std::string_view ddgNodeName(ddg::NodeKind kind) noexcept {
    switch (kind) {
        case ddg::NodeKind::Input: return "input";
        case ddg::NodeKind::Constant: return "constant";
        case ddg::NodeKind::Load: return "load";
        case ddg::NodeKind::Store: return "store";
        case ddg::NodeKind::Operation: return "operation";
        case ddg::NodeKind::Return: return "return";
        case ddg::NodeKind::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view ddgEdgeName(ddg::EdgeKind kind) noexcept {
    switch (kind) {
        case ddg::EdgeKind::DefUse: return "def_use";
        case ddg::EdgeKind::Operand: return "operand";
        case ddg::EdgeKind::Result: return "result";
        case ddg::EdgeKind::ReturnValue: return "return_value";
        case ddg::EdgeKind::Address: return "address";
        case ddg::EdgeKind::Callee: return "callee";
    }
    return "unknown";
}

}  // namespace

std::string DotGenerator::generate() const {
    std::ostringstream output;
    output << "digraph " << quote(graphName()) << " {\n"
           << "  graph [rankdir=\"LR\"];\n";
    emitBody(output);
    output << "}\n";
    return output.str();
}

void DotGenerator::write(const std::filesystem::path& output_path) const {
    if (output_path.empty())
        throw std::invalid_argument("DOT output path cannot be empty");
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot open DOT output: " + output_path.string());
    output << generate();
    output.close();
    if (!output)
        throw std::runtime_error("cannot write DOT output: " + output_path.string());
}

std::string DotGenerator::quote(std::string_view text) {
    std::string result{"\""};
    for (const auto character : text) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(character); break;
        }
    }
    result.push_back('"');
    return result;
}

void DotGenerator::emitNode(std::ostream& output, std::string_view id,
                            std::string_view label,
                            std::string_view attributes) {
    output << "  " << quote(id) << " [label=" << quote(label);
    if (!attributes.empty()) output << ',' << attributes;
    output << "];\n";
}

void DotGenerator::emitEdge(std::ostream& output, std::string_view source,
                            std::string_view target, std::string_view label,
                            std::string_view attributes) {
    output << "  " << quote(source) << " -> " << quote(target)
           << " [label=" << quote(label);
    if (!attributes.empty()) output << ',' << attributes;
    output << "];\n";
}

std::string_view CFGDotGenerator::graphName() const noexcept {
    return kCFGGraphName;
}

void CFGDotGenerator::emitBody(std::ostream& output) const {
    for (const auto& block : graph_.blocks()) {
        std::string label = "block " + std::to_string(block.id) +
                            "\nstart " + std::to_string(block.start_offset) +
                            "\noffsets";
        for (const auto index : block.instruction_indices) {
            if (index < graph_.program().size())
                label += " " + std::to_string(graph_.program()[index].offset);
        }
        emitNode(output, blockId(block.id), label, "shape=box");
    }
    for (const auto& edge : graph_.edges()) {
        auto label = std::string(cfgEdgeName(edge.kind));
        if (edge.kind == cfg::EdgeKind::Exception)
            label += " depth=" + std::to_string(edge.stack_depth);
        emitEdge(output, blockId(edge.source), blockId(edge.target), label);
    }
}

std::string_view CDGDotGenerator::graphName() const noexcept {
    return kCDGGraphName;
}

void CDGDotGenerator::emitBody(std::ostream& output) const {
    for (std::size_t index = 0; index < graph_.blockCount(); ++index) {
        const auto block = static_cast<cfg::BlockId>(index + 1U);
        emitNode(output, blockId(block),
                 "block " + std::to_string(block), "shape=box");
    }
    for (const auto& edge : graph_.edges())
        emitEdge(output, blockId(edge.controller), blockId(edge.dependent),
                 branchOutcomeName(edge.outcome));
}

std::string_view CallGraphDotGenerator::graphName() const noexcept {
    return kCallGraphName;
}

void CallGraphDotGenerator::emitBody(std::ostream& output) const {
    std::set<CodeObjectId> emitted_codes;
    std::set<std::uint64_t> emitted_unknowns;
    for (const auto& edge : graph_.edges()) {
        if (emitted_codes.insert(edge.caller).second)
            emitNode(output, codeId(edge.caller),
                     "code " + std::to_string(edge.caller), "shape=box");
        if (edge.target) {
            if (emitted_codes.insert(edge.target->code).second)
                emitNode(output, codeId(edge.target->code),
                         "code " + std::to_string(edge.target->code), "shape=box");
            emitEdge(output, codeId(edge.caller), codeId(edge.target->code),
                     "site " + std::to_string(edge.site) + " / function " +
                         std::to_string(edge.target->function));
        } else {
            const auto unknown = unknownTargetId(edge.site);
            std::string group_label = "python.dynamic_call";
            if (edge.unresolved_group != 0U) {
                const auto& group = graph_.unresolvedGroup(
                    edge.unresolved_group);
                group_label = bytecode::protocolOperationName(group.operation);
                for (std::uint16_t method_index = 0;
                     method_index < static_cast<std::uint16_t>(
                         bytecode::PythonSpecialMethod::Count);
                     ++method_index) {
                    const auto method = static_cast<bytecode::PythonSpecialMethod>(
                        method_index);
                    if (bytecode::containsPythonMethod(
                            group.candidate_methods, method))
                        group_label += "\n" +
                            std::string(bytecode::specialMethodName(method));
                }
            }
            if (emitted_unknowns.insert(edge.site).second)
                emitNode(output, unknown, group_label, "shape=box");
            emitEdge(output, codeId(edge.caller), unknown,
                     "site " + std::to_string(edge.site), "style=dashed");
        }
    }
}

std::string_view DDGDotGenerator::graphName() const noexcept {
    return kDDGGraphName;
}

void DDGDotGenerator::emitBody(std::ostream& output) const {
    for (const auto& node : graph_.nodes()) {
        auto label = std::string(ddgNodeName(node.kind));
        const auto symbol = graph_.debugString(node.label);
        if (!symbol.empty()) label += " " + std::string(symbol);
        label += "\noffset " + std::to_string(node.bytecode_offset);
        if (node.code != 0U) label += "\ncode " + std::to_string(node.code);
        emitNode(output, dataNodeId(node.id), label, "shape=box");
    }
    for (const auto& edge : graph_.edges())
        emitEdge(output, dataNodeId(edge.source), dataNodeId(edge.target),
                 ddgEdgeName(edge.kind));
}

}  // namespace cpygraph::visualization
