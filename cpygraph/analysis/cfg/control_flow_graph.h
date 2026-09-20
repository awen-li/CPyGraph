#pragma once

#include "bytecode/semantic_ir.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cpygraph::cfg {

using BlockId = std::uint64_t;

struct BasicBlock {
    BlockId id{};
    std::uint32_t start_offset{};
    std::vector<std::size_t> instruction_indices;
};

enum class EdgeKind {
    Fallthrough,
    BranchTrue,
    BranchFalse,
    Jump,
    Exception,
    Resume,
};

struct Edge {
    BlockId source{};
    BlockId target{};
    EdgeKind kind{EdgeKind::Fallthrough};
    std::uint32_t stack_depth{};
    std::uint32_t exception_stack_items{};
    bool push_lasti{false};
};

class ControlFlowGraph {
public:
    ControlFlowGraph(bytecode::SemanticProgram program,
                     std::vector<BasicBlock> blocks,
                     std::vector<Edge> edges);

    const bytecode::SemanticProgram& program() const noexcept { return program_; }
    const std::vector<BasicBlock>& blocks() const noexcept { return blocks_; }
    const std::vector<Edge>& edges() const noexcept { return edges_; }
    const std::vector<BlockId>& instructionBlocks() const noexcept {
        return instruction_blocks_;
    }
    const BasicBlock& block(BlockId id) const;
    BlockId blockForInstruction(std::size_t instruction_index) const;
    BlockId blockAtOffset(std::uint32_t bytecode_offset) const;
    std::vector<BlockId> predecessors(BlockId id) const;
    std::vector<BlockId> successors(BlockId id) const;
    std::vector<Edge> outgoingEdges(BlockId id) const;

private:
    bytecode::SemanticProgram program_;
    std::vector<BasicBlock> blocks_;
    std::vector<Edge> edges_;
    std::vector<BlockId> instruction_blocks_;
};

}  // namespace cpygraph::cfg
