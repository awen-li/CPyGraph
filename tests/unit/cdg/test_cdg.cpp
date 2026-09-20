#include "api/cdg.h"
#include "api/cfg.h"
#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

constexpr std::uint32_t kOffset0 = 0U;
constexpr std::uint32_t kOffset2 = 2U;
constexpr std::uint32_t kOffset4 = 4U;
constexpr std::uint32_t kOffset6 = 6U;
constexpr std::uint32_t kOffset8 = 8U;
constexpr std::uint32_t kOffset10 = 10U;
constexpr cpygraph::cfg::BlockId kBlock1 = 1U;
constexpr cpygraph::cfg::BlockId kBlock2 = 2U;
constexpr cpygraph::cfg::BlockId kBlock3 = 3U;
constexpr cpygraph::cfg::BlockId kBlock4 = 4U;
constexpr cpygraph::cfg::BlockId kNoBlock = 0U;
constexpr std::size_t kInstruction0 = 0U;
constexpr std::size_t kInstruction1 = 1U;
constexpr std::size_t kInstruction2 = 2U;
constexpr std::size_t kInstruction3 = 3U;
constexpr std::size_t kInstruction4 = 4U;
constexpr std::size_t kInstruction5 = 5U;
constexpr std::size_t kDiamondBlockCount = 4U;
constexpr std::size_t kDiamondInstructionCount = 6U;

}  // namespace

int main() {
    using cpygraph::bytecode::SemanticOpcode;
    const auto diamond = cpygraph::cfg::CFGBuilder().build({
        {kOffset0, SemanticOpcode::LoadLocal, {}, "condition"},
        {kOffset2, SemanticOpcode::ConditionalBranch, {}, {}, kOffset8, false},
        {kOffset4, SemanticOpcode::LoadConst},
        {kOffset6, SemanticOpcode::Branch, {}, {}, kOffset10},
        {kOffset8, SemanticOpcode::LoadConst},
        {kOffset10, SemanticOpcode::Return},
    });
    require(diamond.instructionBlocks().size() == kDiamondInstructionCount,
            "CFG exposes one inverse block mapping per instruction");
    require(diamond.blockForInstruction(kInstruction0) == kBlock1 &&
                diamond.blockForInstruction(kInstruction1) == kBlock1 &&
                diamond.blockForInstruction(kInstruction2) == kBlock2 &&
                diamond.blockForInstruction(kInstruction3) == kBlock2 &&
                diamond.blockForInstruction(kInstruction4) == kBlock3 &&
                diamond.blockForInstruction(kInstruction5) == kBlock4,
            "instruction-to-block mapping is complete and ordered");
    require(diamond.blockAtOffset(kOffset8) == kBlock3,
            "CFG maps exact bytecode offsets to blocks");
    bool rejected_unknown_offset = false;
    try {
        static_cast<void>(diamond.blockAtOffset(kOffset8 + 1U));
    } catch (const std::out_of_range&) {
        rejected_unknown_offset = true;
    }
    require(rejected_unknown_offset,
            "CFG rejects offsets that are not instruction boundaries");

    const auto analysis = cpygraph::cdg::CDGBuilder().build(diamond);
    const auto& postdominators = analysis.postdominators;
    require(postdominators.blockCount() == kDiamondBlockCount,
            "postdominator tree covers every CFG block");
    require(postdominators.postdominates(kBlock4, kBlock1) &&
                postdominators.postdominates(kBlock4, kBlock2) &&
                postdominators.postdominates(kBlock4, kBlock3),
            "diamond join postdominates both branch arms and the condition");
    require(postdominators.immediatePostDominator(kBlock1) == kBlock4 &&
                postdominators.immediatePostDominator(kBlock2) == kBlock4 &&
                postdominators.immediatePostDominator(kBlock3) == kBlock4 &&
                postdominators.immediatePostDominator(kBlock4) == kNoBlock,
            "postdominator tree reports the nearest real postdominator");

    const auto& cdg = analysis.graph;
    require(cdg.blockCount() == kDiamondBlockCount,
            "CDG preserves the CFG block domain");
    require(cdg.hasEdge(kBlock1, kBlock2,
                        cpygraph::cdg::BranchOutcome::True) &&
                cdg.hasEdge(kBlock1, kBlock3,
                            cpygraph::cdg::BranchOutcome::False),
            "CDG retains true and false branch outcomes");
    require(!cdg.hasEdge(kBlock1, kBlock4,
                         cpygraph::cdg::BranchOutcome::True),
            "postdominating join is not control dependent on the condition");
    require(cdg.dependents(kBlock1).size() == 2U &&
                cdg.controllers(kBlock2).size() == 1U,
            "CDG exposes controller and dependent queries");

    const auto loop = cpygraph::cfg::CFGBuilder().build({
        {kOffset0, SemanticOpcode::LoadLocal, {}, "condition"},
        {kOffset2, SemanticOpcode::ConditionalBranch, {}, {}, kOffset8, false},
        {kOffset4, SemanticOpcode::LoadConst},
        {kOffset6, SemanticOpcode::Branch, {}, {}, kOffset0},
        {kOffset8, SemanticOpcode::Return},
    });
    const auto loop_analysis = cpygraph::cdg::CDGBuilder().build(loop);
    require(loop_analysis.postdominators.immediatePostDominator(kBlock2) ==
                kBlock1,
            "loop body is immediately postdominated by its header");
    require(loop_analysis.graph.hasEdge(
                kBlock1, kBlock2, cpygraph::cdg::BranchOutcome::True) &&
                loop_analysis.graph.hasEdge(
                    kBlock1, kBlock1,
                    cpygraph::cdg::BranchOutcome::True),
            "loop body and repeated header are controlled by loop continuation");

    const auto exceptional = cpygraph::cfg::CFGBuilder().build({
        {kOffset0, SemanticOpcode::LoadGlobal, {}, "value"},
        {kOffset2, SemanticOpcode::Return},
        {kOffset4, SemanticOpcode::Return, {}, {}, {}, false, {}, {}, true},
    }, {{kOffset0, kOffset2, kOffset4, {}, 1U, false}});
    const auto exceptional_analysis =
        cpygraph::cdg::CDGBuilder().build(exceptional);
    require(exceptional_analysis.graph.hasEdge(
                kBlock1, kBlock2, cpygraph::cdg::BranchOutcome::Normal) &&
                exceptional_analysis.graph.hasEdge(
                    kBlock1, kBlock3,
                    cpygraph::cdg::BranchOutcome::Exception),
            "CDG distinguishes normal and exceptional outcomes");

    const auto non_terminating = cpygraph::cfg::CFGBuilder().build({
        {kOffset0, SemanticOpcode::Branch, {}, {}, kOffset0},
    });
    const auto non_terminating_analysis =
        cpygraph::cdg::CDGBuilder().build(non_terminating);
    require(non_terminating_analysis.postdominators.postdominates(
                kBlock1, kBlock1) &&
                non_terminating_analysis.postdominators.immediatePostDominator(
                    kBlock1) == kNoBlock,
            "synthetic exit keeps postdominators defined for non-terminating regions");
    require(non_terminating_analysis.graph.edges().empty(),
            "an unconditional infinite loop has no branch control dependence");
}
