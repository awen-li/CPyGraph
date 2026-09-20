#include "analysis/cdg/post_dominators.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cpygraph::cdg {
namespace {

constexpr auto kUninitialized = std::numeric_limits<std::size_t>::max();

void validateBlock(cfg::BlockId block, std::size_t count) {
    if (block == 0U || block > count)
        throw std::out_of_range("invalid postdominator block id");
}

std::size_t intersectImmediateDominators(
    std::size_t left, std::size_t right,
    const std::vector<std::size_t>& immediate,
    const std::vector<std::size_t>& reverse_postorder_index) {
    while (left != right) {
        while (reverse_postorder_index[left] > reverse_postorder_index[right])
            left = immediate[left];
        while (reverse_postorder_index[right] > reverse_postorder_index[left])
            right = immediate[right];
    }
    return left;
}

}  // namespace

PostDominatorTree::PostDominatorTree(
    std::vector<cfg::BlockId> immediate_postdominators)
    : immediate_postdominators_(std::move(immediate_postdominators)) {
    for (std::size_t index = 0; index < blockCount(); ++index) {
        const auto immediate = immediate_postdominators_[index];
        if (immediate > blockCount())
            throw std::invalid_argument("invalid immediate postdominator");
        auto current = immediate;
        std::size_t depth = 0U;
        while (current != 0U && depth++ <= blockCount())
            current = immediate_postdominators_[current - 1U];
        if (depth > blockCount())
            throw std::invalid_argument(
                "immediate postdominator tree contains a cycle");
    }
}

bool PostDominatorTree::postdominates(
    cfg::BlockId candidate, cfg::BlockId block) const {
    validateBlock(candidate, blockCount());
    validateBlock(block, blockCount());
    auto current = block;
    while (current != 0U) {
        if (current == candidate) return true;
        current = immediate_postdominators_[current - 1U];
    }
    return false;
}

cfg::BlockId PostDominatorTree::immediatePostDominator(
    cfg::BlockId block) const {
    validateBlock(block, blockCount());
    return immediate_postdominators_[block - 1U];
}

std::vector<cfg::BlockId> PostDominatorTree::postdominators(
    cfg::BlockId block) const {
    validateBlock(block, blockCount());
    std::vector<cfg::BlockId> result;
    auto current = block;
    while (current != 0U) {
        result.push_back(current);
        current = immediate_postdominators_[current - 1U];
    }
    std::sort(result.begin(), result.end());
    return result;
}

PostDominatorTree PostDominatorBuilder::build(
    const cfg::ControlFlowGraph& graph) const {
    const auto block_count = graph.blocks().size();
    if (block_count == 0U) return {};
    const auto virtual_exit = block_count;
    const auto node_count = block_count + 1U;
    std::vector<std::vector<std::size_t>> successors(block_count);
    std::vector<std::vector<std::size_t>> predecessors(block_count);
    for (const auto& edge : graph.edges()) {
        const auto source = static_cast<std::size_t>(edge.source - 1U);
        const auto target = static_cast<std::size_t>(edge.target - 1U);
        if (std::find(successors[source].begin(), successors[source].end(), target) ==
            successors[source].end()) {
            successors[source].push_back(target);
            predecessors[target].push_back(source);
        }
    }

    std::vector<unsigned char> reaches_real_exit(block_count);
    std::vector<std::size_t> pending;
    for (std::size_t block = 0; block < block_count; ++block) {
        if (successors[block].empty()) {
            reaches_real_exit[block] = 1U;
            pending.push_back(block);
        }
    }
    while (!pending.empty()) {
        const auto block = pending.back();
        pending.pop_back();
        for (const auto predecessor : predecessors[block]) {
            if (reaches_real_exit[predecessor]) continue;
            reaches_real_exit[predecessor] = 1U;
            pending.push_back(predecessor);
        }
    }

    std::vector<std::vector<std::size_t>> augmented_successors = successors;
    for (std::size_t block = 0; block < block_count; ++block)
        if (successors[block].empty() || !reaches_real_exit[block])
            augmented_successors[block].push_back(virtual_exit);

    std::vector<std::vector<std::size_t>> reverse_successors(node_count);
    for (std::size_t source = 0; source < block_count; ++source)
        for (const auto target : augmented_successors[source])
            reverse_successors[target].push_back(source);

    std::vector<unsigned char> visited(node_count);
    std::vector<std::pair<std::size_t, std::size_t>> stack;
    std::vector<std::size_t> postorder;
    visited[virtual_exit] = 1U;
    stack.push_back({virtual_exit, 0U});
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.second < reverse_successors[frame.first].size()) {
            const auto successor = reverse_successors[frame.first][frame.second++];
            if (!visited[successor]) {
                visited[successor] = 1U;
                stack.push_back({successor, 0U});
            }
            continue;
        }
        postorder.push_back(frame.first);
        stack.pop_back();
    }
    if (postorder.size() != node_count)
        throw std::logic_error(
            "augmented CFG does not reach the synthetic exit");
    std::vector<std::size_t> reverse_postorder(
        postorder.rbegin(), postorder.rend());
    std::vector<std::size_t> reverse_postorder_index(node_count);
    for (std::size_t index = 0; index < reverse_postorder.size(); ++index)
        reverse_postorder_index[reverse_postorder[index]] = index;

    std::vector<std::size_t> immediate(node_count, kUninitialized);
    immediate[virtual_exit] = virtual_exit;
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = 1U; index < reverse_postorder.size(); ++index) {
            const auto block = reverse_postorder[index];
            const auto& candidates = augmented_successors[block];
            const auto first = std::find_if(
                candidates.begin(), candidates.end(), [&](const auto candidate) {
                    return immediate[candidate] != kUninitialized;
                });
            if (first == candidates.end()) continue;
            auto next = *first;
            for (auto candidate = std::next(first);
                 candidate != candidates.end(); ++candidate) {
                if (immediate[*candidate] == kUninitialized) continue;
                next = intersectImmediateDominators(
                    next, *candidate, immediate, reverse_postorder_index);
            }
            if (immediate[block] != next) {
                immediate[block] = next;
                changed = true;
            }
        }
    }

    std::vector<cfg::BlockId> result(block_count);
    for (std::size_t block = 0; block < block_count; ++block) {
        if (immediate[block] == kUninitialized)
            throw std::logic_error(
                "postdominator construction did not converge");
        result[block] = immediate[block] == virtual_exit
            ? 0U : static_cast<cfg::BlockId>(immediate[block] + 1U);
    }
    return PostDominatorTree(std::move(result));
}

}  // namespace cpygraph::cdg
