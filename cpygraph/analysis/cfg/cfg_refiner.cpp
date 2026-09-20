#include "analysis/cfg/cfg_refiner.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace cpygraph::cfg {
namespace {

using Names = std::unordered_set<std::string>;

void transfer(const bytecode::SemanticInstruction& instruction, Names& assigned) {
    using O = bytecode::SemanticOpcode;
    switch (instruction.opcode) {
        case O::StoreLocal:
            assigned.insert(instruction.symbol);
            break;
        case O::StoreLocalPair:
            assigned.insert(instruction.symbol);
            assigned.insert(instruction.secondary_symbol);
            break;
        case O::StoreLoadLocal:
            assigned.insert(instruction.symbol);
            break;
        case O::DeleteLocal:
            assigned.erase(instruction.symbol);
            break;
        case O::LoadLocal:
            if (instruction.clear_local_after_load)
                assigned.erase(instruction.symbol);
            break;
        default:
            break;
    }
}

Names intersection(Names left, const Names& right) {
    for (auto item = left.begin(); item != left.end();) {
        if (right.count(*item) == 0U)
            item = left.erase(item);
        else
            ++item;
    }
    return left;
}

}  // namespace

ControlFlowGraph CFGRefiner::refine(
    const ControlFlowGraph& graph,
    const std::vector<std::string>& initially_assigned) const {
    if (graph.blocks().empty()) return graph;

    Names universe(initially_assigned.begin(), initially_assigned.end());
    for (const auto& instruction : graph.program()) {
        using O = bytecode::SemanticOpcode;
        if (instruction.opcode == O::LoadLocal ||
            instruction.opcode == O::StoreLocal ||
            instruction.opcode == O::DeleteLocal ||
            instruction.opcode == O::StoreLoadLocal)
            universe.insert(instruction.symbol);
        if (instruction.opcode == O::StoreLocalPair) {
            universe.insert(instruction.symbol);
            universe.insert(instruction.secondary_symbol);
        }
    }

    std::vector<Names> incoming(graph.blocks().size() + 1U, universe);
    std::vector<Names> outgoing(graph.blocks().size() + 1U, universe);
    incoming[1] = Names(initially_assigned.begin(), initially_assigned.end());
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& block : graph.blocks()) {
            Names next_in;
            if (block.id == 1U) {
                next_in = Names(initially_assigned.begin(), initially_assigned.end());
            } else {
                const auto predecessors = graph.predecessors(block.id);
                if (!predecessors.empty()) {
                    next_in = outgoing[predecessors.front()];
                    for (std::size_t index = 1; index < predecessors.size(); ++index)
                        next_in = intersection(std::move(next_in),
                                               outgoing[predecessors[index]]);
                }
            }
            Names next_out = next_in;
            for (const auto index : block.instruction_indices)
                transfer(graph.program()[index], next_out);
            if (next_in != incoming[block.id] || next_out != outgoing[block.id]) {
                incoming[block.id] = std::move(next_in);
                outgoing[block.id] = std::move(next_out);
                changed = true;
            }
        }
    }

    std::vector<Edge> edges;
    edges.reserve(graph.edges().size());
    for (const auto& edge : graph.edges()) {
        bool disproved = false;
        if (edge.kind == EdgeKind::Exception) {
            const auto& block = graph.block(edge.source);
            Names before_last = incoming[block.id];
            for (std::size_t position = 0;
                 position + 1U < block.instruction_indices.size(); ++position)
                transfer(graph.program()[block.instruction_indices[position]],
                         before_last);
            const auto& last =
                graph.program()[block.instruction_indices.back()];
            disproved = last.opcode == bytecode::SemanticOpcode::LoadLocal &&
                        !last.clear_local_after_load &&
                        before_last.count(last.symbol) != 0U;
        }
        if (!disproved) edges.push_back(edge);
    }
    return ControlFlowGraph(graph.program(), graph.blocks(), std::move(edges));
}

}  // namespace cpygraph::cfg
