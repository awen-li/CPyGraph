#include "analysis/cfg/control_flow_graph.h"

#include <algorithm>
#include <stdexcept>

namespace cpygraph::cfg {

ControlFlowGraph::ControlFlowGraph(bytecode::SemanticProgram program,
                                   std::vector<BasicBlock> blocks,
                                   std::vector<Edge> edges)
    : program_(std::move(program)), blocks_(std::move(blocks)), edges_(std::move(edges)),
      instruction_blocks_(program_.size()) {
    if (program_.empty() != blocks_.empty())
        throw std::invalid_argument("CFG program and block table must both be empty or nonempty");
    std::size_t expected_index = 0;
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        const auto& current = blocks_[i];
        if (current.id != i + 1 || current.instruction_indices.empty())
            throw std::invalid_argument("CFG blocks require sequential ids and instructions");
        if (current.instruction_indices.front() >= program_.size() ||
            current.start_offset != program_[current.instruction_indices.front()].offset)
            throw std::invalid_argument("CFG block start offset does not match its first instruction");
        for (const auto index : current.instruction_indices) {
            if (index != expected_index++)
                throw std::invalid_argument("CFG blocks must partition instructions in program order");
            instruction_blocks_[index] = current.id;
        }
    }
    if (expected_index != program_.size())
        throw std::invalid_argument("CFG blocks do not cover every instruction");
    for (const auto& edge : edges_) {
        if (edge.source == 0 || edge.source > blocks_.size() ||
            edge.target == 0 || edge.target > blocks_.size())
            throw std::invalid_argument("CFG edge references an invalid block");
        if (edge.kind == EdgeKind::Exception) {
            if (edge.exception_stack_items == 0)
                throw std::invalid_argument("CFG exception edge must provide an exception value");
        } else if (edge.stack_depth != 0 || edge.exception_stack_items != 0 || edge.push_lasti) {
            throw std::invalid_argument("ordinary CFG edge carries exception transfer metadata");
        }
    }
}

BlockId ControlFlowGraph::blockForInstruction(
    std::size_t instruction_index) const {
    if (instruction_index >= instruction_blocks_.size())
        throw std::out_of_range("invalid CFG instruction index");
    return instruction_blocks_[instruction_index];
}

BlockId ControlFlowGraph::blockAtOffset(std::uint32_t bytecode_offset) const {
    const auto found = std::lower_bound(
        program_.begin(), program_.end(), bytecode_offset,
        [](const auto& instruction, const auto offset) {
            return instruction.offset < offset;
        });
    if (found == program_.end() || found->offset != bytecode_offset)
        throw std::out_of_range("bytecode offset is not a CFG instruction boundary");
    return blockForInstruction(
        static_cast<std::size_t>(std::distance(program_.begin(), found)));
}

const BasicBlock& ControlFlowGraph::block(BlockId id) const {
    if (id == 0 || id > blocks_.size()) throw std::out_of_range("invalid CFG block id");
    return blocks_[id - 1];
}

std::vector<BlockId> ControlFlowGraph::predecessors(BlockId id) const {
    block(id);
    std::vector<BlockId> result;
    for (const auto& edge : edges_) if (edge.target == id) result.push_back(edge.source);
    return result;
}

std::vector<BlockId> ControlFlowGraph::successors(BlockId id) const {
    block(id);
    std::vector<BlockId> result;
    for (const auto& edge : edges_) if (edge.source == id) result.push_back(edge.target);
    return result;
}

std::vector<Edge> ControlFlowGraph::outgoingEdges(BlockId id) const {
    block(id);
    std::vector<Edge> result;
    for (const auto& edge : edges_)
        if (edge.source == id) result.push_back(edge);
    return result;
}

}  // namespace cpygraph::cfg
