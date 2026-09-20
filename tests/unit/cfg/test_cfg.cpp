#include "api/cfg.h"
#include "api/ddg.h"
#include "test_support.h"

#include <stdexcept>

namespace {
bool hasEdge(const cpygraph::cfg::ControlFlowGraph& graph, cpygraph::cfg::BlockId source,
             cpygraph::cfg::BlockId target, cpygraph::cfg::EdgeKind kind) {
    for (const auto& edge : graph.edges())
        if (edge.source == source && edge.target == target && edge.kind == kind) return true;
    return false;
}

cpygraph::ddg::NodeId nodeAt(const cpygraph::ddg::DataDependencyGraph& graph,
                              std::uint32_t offset) {
    for (const auto& node : graph.nodes())
        if (node.bytecode_offset == offset) return node.id;
    return 0;
}
}

int main() {
    using namespace cpygraph::bytecode;
    SemanticProgram program{
        {0, SemanticOpcode::LoadGlobal, 0, "condition"},
        {2, SemanticOpcode::ConditionalBranch, 0, "", 8, false},
        {4, SemanticOpcode::LoadConst, 0},
        {6, SemanticOpcode::Branch, 0, "", 10},
        {8, SemanticOpcode::LoadConst, 1},
        {10, SemanticOpcode::Return},
    };
    const auto graph = cpygraph::cfg::CFGBuilder().build(std::move(program));
    require(graph.blocks().size() == 4, "CFG creates leaders for target and post-branch instructions");
    require(graph.edges().size() == 4, "CFG creates conditional, jump, and fallthrough edges");
    require(graph.successors(1).size() == 2, "conditional block has two successors");
    require(graph.outgoingEdges(1).size() == 2,
            "CFG exposes typed outgoing edges for edge-specific transfer");
    require(hasEdge(graph, 1, 3, cpygraph::cfg::EdgeKind::BranchFalse) &&
            hasEdge(graph, 1, 2, cpygraph::cfg::EdgeKind::BranchTrue),
            "CFG labels conditional target and fallthrough edges with the correct polarity");
    require(hasEdge(graph, 2, 4, cpygraph::cfg::EdgeKind::Jump) &&
            hasEdge(graph, 3, 4, cpygraph::cfg::EdgeKind::Fallthrough),
            "CFG distinguishes explicit jumps from ordinary fallthrough");
    require(graph.predecessors(4).size() == 2, "join block has both predecessors");
    require(graph.successors(4).empty(), "return terminates control flow");

    SemanticProgram loop{
        {0, SemanticOpcode::LoadGlobal, 0, "condition"},
        {2, SemanticOpcode::ConditionalBranch, 0, "", 8, false},
        {4, SemanticOpcode::LoadConst, 0},
        {6, SemanticOpcode::Branch, 0, "", 0},
        {8, SemanticOpcode::Return},
    };
    const auto loop_graph = cpygraph::cfg::CFGBuilder().build(std::move(loop));
    require(loop_graph.blocks().size() == 3, "CFG represents backward-loop leader");
    require(loop_graph.predecessors(1).size() == 1, "CFG retains backward edge to loop header");
    require(hasEdge(loop_graph, 2, 1, cpygraph::cfg::EdgeKind::Jump),
            "CFG labels backward loop control as an explicit jump");

    SemanticProgram suspended{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::Suspend, 0, "", std::nullopt, false, 0, 0,
         false, 1, 1},
        {4, SemanticOpcode::Pop},
        {6, SemanticOpcode::Return, 0, "", std::nullopt, false, 0, 0, true},
    };
    const auto suspended_graph =
        cpygraph::cfg::CFGBuilder().build(std::move(suspended));
    require(suspended_graph.blocks().size() == 2,
            "CFG splits control at a suspension point");
    require(hasEdge(suspended_graph, 1, 2, cpygraph::cfg::EdgeKind::Resume),
            "CFG distinguishes resumption from ordinary fallthrough");

    SemanticProgram initialized_local{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::StoreLocal, 0, "value"},
        {4, SemanticOpcode::LoadLocal, 0, "value"},
        {6, SemanticOpcode::Return},
        {8, SemanticOpcode::Return, 0, "", std::nullopt, false, 0, 0, true},
    };
    const auto initialized_graph = cpygraph::cfg::CFGBuilder().build(
        std::move(initialized_local), {{4, 6, 8, 0, 1, false}});
    require(hasEdge(initialized_graph, 2, 4, cpygraph::cfg::EdgeKind::Exception),
            "syntactic CFG conservatively emits an unbound-local exception edge");
    const auto refined_initialized =
        cpygraph::cfg::CFGRefiner().refine(initialized_graph);
    require(!hasEdge(refined_initialized, 2, 4,
                     cpygraph::cfg::EdgeKind::Exception),
            "definite assignment removes an impossible local-load exception");

    SemanticProgram protected_program{
        {0, SemanticOpcode::LoadGlobal, 0, "source"},
        {2, SemanticOpcode::StoreLocal, 0, "value"},
        {4, SemanticOpcode::Branch, 0, "", 8},
        {6, SemanticOpcode::Return},
        {8, SemanticOpcode::Return, 0, "", std::nullopt, false, 0, 0, true},
    };
    const auto protected_graph = cpygraph::cfg::CFGBuilder().build(
        protected_program, {{0, 4, 6, 0, 1, false}});
    require(hasEdge(protected_graph, 1, 4, cpygraph::cfg::EdgeKind::Exception) &&
                hasEdge(protected_graph, 2, 4, cpygraph::cfg::EdgeKind::Exception),
            "CFG splits protected operations and connects each possible throw to its handler");
    const auto protected_ddg = cpygraph::ddg::DDGBuilder().build(protected_graph);
    cpygraph::ddg::NodeId exception_input = 0;
    cpygraph::ddg::NodeId handler_return = 0;
    for (const auto& node : protected_ddg.nodes()) {
        if (protected_ddg.debugString(node.label) == "exception state")
            exception_input = node.id;
        if (node.kind == cpygraph::ddg::NodeKind::Return && node.bytecode_offset == 6)
            handler_return = node.id;
    }
    require(exception_input != 0 && protected_ddg.hasEdge(
                exception_input, handler_return,
                cpygraph::ddg::EdgeKind::ReturnValue),
            "DDG initializes handler operands from explicit unknown exception state");
    require(protected_ddg.hasEdge(nodeAt(protected_ddg, 0), exception_input,
                                  cpygraph::ddg::EdgeKind::Result) ||
                protected_ddg.hasEdge(nodeAt(protected_ddg, 2), exception_input,
                                      cpygraph::ddg::EdgeKind::Result),
            "DDG relates handler exception state to the protected operation that raised it");
    bool rejected_bad_exception_boundary = false;
    try {
        cpygraph::cfg::CFGBuilder().build(protected_program,
                                            {{0, 3, 6, 0, 1, false}});
    } catch (const std::invalid_argument&) { rejected_bad_exception_boundary = true; }
    require(rejected_bad_exception_boundary,
            "CFG rejects malformed normalized exception boundaries");

    SemanticProgram jump_on_true{
        {0, SemanticOpcode::LoadGlobal, 0, "condition"},
        {2, SemanticOpcode::ConditionalBranch, 0, "", 6, true},
        {4, SemanticOpcode::Return, 0, "", std::nullopt, false, 0, 0, true},
        {6, SemanticOpcode::Return, 0, "", std::nullopt, false, 0, 0, true},
    };
    const auto true_graph = cpygraph::cfg::CFGBuilder().build(std::move(jump_on_true));
    require(hasEdge(true_graph, 1, 3, cpygraph::cfg::EdgeKind::BranchTrue) &&
            hasEdge(true_graph, 1, 2, cpygraph::cfg::EdgeKind::BranchFalse),
            "CFG honors adapter-normalized jump-on-true polarity");

    bool rejected_missing_target = false;
    try {
        cpygraph::cfg::CFGBuilder().build({{0, SemanticOpcode::Branch}});
    } catch (const std::invalid_argument&) { rejected_missing_target = true; }
    require(rejected_missing_target, "CFG rejects a branch without a target");

    bool rejected_spurious_target = false;
    try {
        cpygraph::cfg::CFGBuilder().build({{0, SemanticOpcode::LoadConst, 0, "", 0}});
    } catch (const std::invalid_argument&) { rejected_spurious_target = true; }
    require(rejected_spurious_target, "CFG rejects jump metadata on a non-branch instruction");

    bool rejected_missing_fallthrough = false;
    try {
        cpygraph::cfg::CFGBuilder().build(
            {{0, SemanticOpcode::ConditionalBranch, 0, "", 0}});
    } catch (const std::invalid_argument&) { rejected_missing_fallthrough = true; }
    require(rejected_missing_fallthrough,
            "CFG rejects a terminal conditional branch without a fallthrough instruction");

    bool rejected_bad_boundary = false;
    try {
        cpygraph::cfg::CFGBuilder().build({
            {0, SemanticOpcode::Branch, 0, "", 1}, {2, SemanticOpcode::Return, 0, "",
                                                    std::nullopt, false, 0, 0, true}});
    } catch (const std::invalid_argument&) { rejected_bad_boundary = true; }
    require(rejected_bad_boundary, "CFG rejects targets that are not instruction boundaries");

    bool rejected_spurious_direction = false;
    try {
        cpygraph::cfg::CFGBuilder().build({
            {0, SemanticOpcode::LoadConst, 0, "", std::nullopt, true}});
    } catch (const std::invalid_argument&) { rejected_spurious_direction = true; }
    require(rejected_spurious_direction,
            "CFG rejects a conditional-direction flag on a non-conditional instruction");

    bool rejected_spurious_constant = false;
    try {
        cpygraph::cfg::CFGBuilder().build({
            {0, SemanticOpcode::LoadConst, 0, "", std::nullopt, false, 0, 0, true}});
    } catch (const std::invalid_argument&) { rejected_spurious_constant = true; }
    require(rejected_spurious_constant,
            "CFG rejects implicit-return metadata on a non-return instruction");

    bool rejected_malformed_blocks = false;
    try {
        cpygraph::cfg::ControlFlowGraph malformed(
            {{0, SemanticOpcode::Nop}}, {{2, 0, {0}}}, {});
    } catch (const std::invalid_argument&) { rejected_malformed_blocks = true; }
    require(rejected_malformed_blocks,
            "CFG constructor rejects non-sequential externally supplied block ids");

    bool rejected_dangling_edge = false;
    try {
        cpygraph::cfg::ControlFlowGraph malformed(
            {{0, SemanticOpcode::Nop}}, {{1, 0, {0}}},
            {{1, 2, cpygraph::cfg::EdgeKind::Fallthrough}});
    } catch (const std::invalid_argument&) { rejected_dangling_edge = true; }
    require(rejected_dangling_edge,
            "CFG constructor rejects edges that reference nonexistent blocks");
}
