#include "analysis/cfg/cfg_builder.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace cpygraph::cfg {
namespace {

bool isSuspension(bytecode::SemanticOpcode opcode) {
    return opcode == bytecode::SemanticOpcode::Suspend ||
           opcode == bytecode::SemanticOpcode::Yield;
}

bool isTerminator(bytecode::SemanticOpcode opcode) {
    return opcode == bytecode::SemanticOpcode::Return ||
           opcode == bytecode::SemanticOpcode::Raise ||
           opcode == bytecode::SemanticOpcode::Branch ||
           opcode == bytecode::SemanticOpcode::ConditionalBranch ||
           isSuspension(opcode);
}

bool mayRaise(bytecode::SemanticOpcode opcode) {
    using O = bytecode::SemanticOpcode;
    return opcode != O::Nop && opcode != O::PrepareKeywordCall &&
           opcode != O::LoadConst && opcode != O::LoadLiteral &&
           opcode != O::CallProtocolMarker && opcode != O::StackCopy &&
           opcode != O::StackSwap && opcode != O::StackRotate &&
           opcode != O::Pop && opcode != O::Branch && opcode != O::Return;
}

}  // namespace

ControlFlowGraph CFGBuilder::build(
    bytecode::SemanticProgram program,
    const std::vector<bytecode::ExceptionRegion>& exception_regions) const {
    if (program.empty()) return ControlFlowGraph({}, {}, {});
    std::unordered_map<std::uint32_t, std::size_t> offset_to_index;
    for (std::size_t i = 0; i < program.size(); ++i) {
        if (i && program[i - 1].offset >= program[i].offset)
            throw std::invalid_argument("semantic instructions must have unique increasing offsets");
        offset_to_index.emplace(program[i].offset, i);
    }
    std::set<std::size_t> leaders{0};
    for (const auto& region : exception_regions) {
        if (region.start_offset >= region.end_offset || region.exception_stack_items == 0)
            throw std::invalid_argument("invalid normalized exception region");
        if (region.stack_depth > program.size() || region.exception_stack_items > 8)
            throw std::invalid_argument("exception region has implausible stack metadata");
        const auto start = offset_to_index.find(region.start_offset);
        const auto handler = offset_to_index.find(region.handler_offset);
        if (start == offset_to_index.end() || handler == offset_to_index.end())
            throw std::invalid_argument("exception region boundary is not an instruction boundary");
        leaders.insert(start->second);
        leaders.insert(handler->second);
        const auto end = offset_to_index.find(region.end_offset);
        if (end != offset_to_index.end()) leaders.insert(end->second);
        else if (region.end_offset <= program.back().offset)
            throw std::invalid_argument("exception region end is not an instruction boundary");
        for (std::size_t index = start->second;
             index < program.size() && program[index].offset < region.end_offset; ++index) {
            if (mayRaise(program[index].opcode) && index + 1 < program.size())
                leaders.insert(index + 1);
        }
    }
    for (std::size_t i = 0; i < program.size(); ++i) {
        const auto& instruction = program[i];
        const bool is_branch = instruction.opcode == bytecode::SemanticOpcode::Branch ||
                               instruction.opcode == bytecode::SemanticOpcode::ConditionalBranch;
        if (is_branch != instruction.jump_target.has_value())
            throw std::invalid_argument(is_branch ? "branch has no target" :
                                                    "non-branch instruction has a jump target");
        if (instruction.jump_on_true &&
            instruction.opcode != bytecode::SemanticOpcode::ConditionalBranch)
            throw std::invalid_argument("true-branch flag appears on a non-conditional instruction");
        if (instruction.implicit_constant &&
            instruction.opcode != bytecode::SemanticOpcode::Return)
            throw std::invalid_argument("implicit constant metadata appears on a non-return instruction");
        if (instruction.opcode == bytecode::SemanticOpcode::ConditionalBranch &&
            i + 1 == program.size())
            throw std::invalid_argument("conditional branch has no fallthrough instruction");
        if (instruction.jump_target) {
            const auto found = offset_to_index.find(*instruction.jump_target);
            if (found == offset_to_index.end()) throw std::invalid_argument("jump target is not an instruction boundary");
            leaders.insert(found->second);
        }
        if (isTerminator(instruction.opcode) && i + 1 < program.size()) leaders.insert(i + 1);
    }

    std::vector<std::size_t> starts(leaders.begin(), leaders.end());
    std::vector<BasicBlock> blocks;
    std::unordered_map<std::size_t, BlockId> index_to_block;
    for (std::size_t b = 0; b < starts.size(); ++b) {
        const auto end = b + 1 < starts.size() ? starts[b + 1] : program.size();
        BasicBlock block{b + 1, program[starts[b]].offset, {}};
        for (std::size_t i = starts[b]; i < end; ++i) {
            block.instruction_indices.push_back(i);
            index_to_block[i] = block.id;
        }
        blocks.push_back(std::move(block));
    }

    std::vector<Edge> edges;
    const auto add_edge = [&](Edge edge) {
        const auto duplicate = std::find_if(edges.begin(), edges.end(), [&](const Edge& current) {
            return current.source == edge.source && current.target == edge.target &&
                   current.kind == edge.kind && current.stack_depth == edge.stack_depth &&
                   current.exception_stack_items == edge.exception_stack_items &&
                   current.push_lasti == edge.push_lasti;
        });
        if (duplicate == edges.end()) edges.push_back(edge);
    };
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        const auto& last = program[blocks[b].instruction_indices.back()];
        const auto add_target = [&](EdgeKind kind) {
            const auto target_index = offset_to_index.at(*last.jump_target);
            add_edge({blocks[b].id, index_to_block.at(target_index), kind});
        };
        if (last.opcode == bytecode::SemanticOpcode::Branch) {
            add_target(EdgeKind::Jump);
        } else if (last.opcode == bytecode::SemanticOpcode::ConditionalBranch) {
            add_target(last.jump_on_true ? EdgeKind::BranchTrue : EdgeKind::BranchFalse);
            if (b + 1 < blocks.size()) {
                add_edge({blocks[b].id, blocks[b + 1].id,
                    last.jump_on_true ? EdgeKind::BranchFalse : EdgeKind::BranchTrue});
            }
        } else if (isSuspension(last.opcode)) {
            if (b + 1 < blocks.size())
                add_edge({blocks[b].id, blocks[b + 1].id, EdgeKind::Resume});
        } else if (last.opcode != bytecode::SemanticOpcode::Return &&
                   last.opcode != bytecode::SemanticOpcode::Raise && b + 1 < blocks.size()) {
            add_edge({blocks[b].id, blocks[b + 1].id, EdgeKind::Fallthrough});
        }
    }
    for (const auto& region : exception_regions) {
        const auto target = index_to_block.at(offset_to_index.at(region.handler_offset));
        for (const auto& block : blocks) {
            const auto last_index = block.instruction_indices.back();
            const auto& instruction = program[last_index];
            if (instruction.offset >= region.start_offset &&
                instruction.offset < region.end_offset && mayRaise(instruction.opcode)) {
                add_edge({block.id, target, EdgeKind::Exception, region.stack_depth,
                          region.exception_stack_items, region.push_lasti});
            }
        }
    }
    return ControlFlowGraph(std::move(program), std::move(blocks), std::move(edges));
}

}  // namespace cpygraph::cfg
