#include "analysis/ddg/ddg_builder.h"

#include <algorithm>
#include <array>
#include <deque>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace cpygraph::ddg {
namespace {

using ValueSet = std::set<NodeId>;

constexpr std::uint32_t kSingleCallProtocolInput = 1U;
constexpr std::uint32_t kDualCallProtocolInputs = 2U;
constexpr std::uint32_t kSingleStackOutput = 1U;
constexpr std::uint32_t kStackOutputIndexBias = 1U;
constexpr char kOperationResultLabel[] = "value operation result ";

struct State {
    bool initialized{false};
    std::vector<ValueSet> stack;
    std::unordered_map<std::string, ValueSet> locals;
    std::unordered_map<std::string, ValueSet> globals;
};

bool mergeSet(ValueSet& target, const ValueSet& source) {
    const auto old_size = target.size();
    target.insert(source.begin(), source.end());
    return target.size() != old_size;
}

bool mergeState(State& target, const State& source, std::uint32_t source_offset,
                std::uint32_t join_offset) {
    if (!source.initialized) return false;
    if (!target.initialized) { target = source; return true; }
    if (target.stack.size() != source.stack.size())
        throw std::runtime_error("incompatible operand-stack heights at CFG join at bytecode offset " +
            std::to_string(join_offset) + ": expected " + std::to_string(target.stack.size()) +
            ", received " + std::to_string(source.stack.size()) + " from block at bytecode offset " +
            std::to_string(source_offset));
    bool changed = false;
    for (std::size_t i = 0; i < target.stack.size(); ++i) changed |= mergeSet(target.stack[i], source.stack[i]);
    for (const auto& [name, values] : source.locals) changed |= mergeSet(target.locals[name], values);
    for (const auto& [name, values] : source.globals) changed |= mergeSet(target.globals[name], values);
    return changed;
}

ValueSet pop(State& state, std::uint32_t offset) {
    if (state.stack.empty()) throw std::runtime_error("operand-stack underflow at bytecode offset " + std::to_string(offset));
    auto value = std::move(state.stack.back());
    state.stack.pop_back();
    return value;
}

void requireStack(const State& state, std::size_t required,
                  std::uint32_t offset, const char* operation) {
    if (required <= state.stack.size()) return;
    throw std::runtime_error(std::string("operand-stack underflow for ") + operation +
        " at bytecode offset " + std::to_string(offset) + ": requires " +
        std::to_string(required) + ", has " + std::to_string(state.stack.size()));
}

const char* operationLabel(bytecode::SemanticOpcode opcode) {
    using O = bytecode::SemanticOpcode;
    switch (opcode) {
        case O::LoadConst: return "load constant";
        case O::LoadLocal: return "load local";
        case O::StoreLocal: return "store local";
        case O::LoadGlobal: return "load global";
        case O::StoreGlobal: return "store global";
        case O::LoadAttribute: return "load attribute";
        case O::StoreAttribute: return "store attribute";
        case O::ImportModule: return "import";
        case O::ImportAttribute: return "import attribute";
        case O::EnterContext: return "enter context";
        case O::CreateFunction: return "create function";
        case O::SetFunctionAttribute: return "set function attribute";
        case O::Call: return "call";
        case O::PrepareKeywordCall: return "keyword call metadata";
        case O::CallProtocolMarker: return "call protocol marker";
        case O::Generic: return "value operation";
        case O::BuildCollection: return "collection construction";
        case O::UnpackCollection: return "collection unpack";
        case O::Suspend: return "suspend";
        case O::Yield: return "yield";
        case O::LoadLiteral: return "load literal";
        case O::StackCopy: return "copy stack value";
        case O::StackSwap: return "swap stack values";
        case O::StackRotate: return "rotate stack values";
        case O::LoadElement: return "load element";
        case O::StoreElement: return "store element";
        case O::StoreCollectionElement: return "store collection element";
        case O::DeleteElement: return "delete element";
        case O::DeleteLocal: return "delete local";
        case O::DeleteGlobal: return "delete global";
        case O::LoadLocalPair: return "load local pair";
        case O::StoreLocalPair: return "store local pair";
        case O::StoreLoadLocal: return "store/load local pair";
        case O::Raise: return "raise";
        case O::Return: return "return";
        case O::Pop: return "pop";
        case O::Branch: return "branch";
        case O::ConditionalBranch: return "conditional branch";
        case O::Nop: return "nop";
        case O::Unsupported: return "unsupported";
    }
    return "unknown";
}

NodeKind nodeKind(bytecode::SemanticOpcode opcode) {
    using O = bytecode::SemanticOpcode;
    if (opcode == O::LoadConst || opcode == O::LoadLiteral) return NodeKind::Constant;
    if (opcode == O::LoadLocal || opcode == O::LoadGlobal ||
        opcode == O::LoadAttribute || opcode == O::LoadElement)
        return NodeKind::Load;
    if (opcode == O::StoreLocal || opcode == O::StoreGlobal ||
        opcode == O::StoreAttribute || opcode == O::StoreElement ||
        opcode == O::StoreCollectionElement)
        return NodeKind::Store;
    if (opcode == O::Return) return NodeKind::Return;
    return NodeKind::Operation;
}

}  // namespace

DataDependencyGraph DDGBuilder::build(const cfg::ControlFlowGraph& cfg,
                                      CodeObjectId code) const {
    return std::move(buildWithBoundaries(cfg, code, {}).graph);
}

DDGBuildResult DDGBuilder::buildWithBoundaries(
    const cfg::ControlFlowGraph& cfg, CodeObjectId code,
    const std::vector<std::string>& parameter_names) const {
    DDGBuildResult result;
    auto& graph = result.graph;
    if (cfg.blocks().empty()) return result;
    const auto& program = cfg.program();
    std::vector<NodeId> instruction_nodes;
    std::vector<NodeId> secondary_instruction_nodes(program.size(), 0U);
    std::vector<NodeId> implicit_constants(program.size(), 0);
    instruction_nodes.reserve(program.size());
    for (std::size_t index = 0; index < program.size(); ++index) {
        const auto& instruction = program[index];
        auto label = std::string(operationLabel(instruction.opcode));
        if (!instruction.symbol.empty()) label += " " + instruction.symbol;
        auto kind = nodeKind(instruction.opcode);
        auto secondary_kind = NodeKind::Unknown;
        std::string secondary_label;
        using O = bytecode::SemanticOpcode;
        if (instruction.opcode == O::LoadLocalPair) {
            kind = NodeKind::Load;
            label = "load local " + instruction.symbol;
            secondary_kind = NodeKind::Load;
            secondary_label = "load local " + instruction.secondary_symbol;
        } else if (instruction.opcode == O::StoreLocalPair) {
            kind = NodeKind::Store;
            label = "store local " + instruction.symbol;
            secondary_kind = NodeKind::Store;
            secondary_label = "store local " + instruction.secondary_symbol;
        } else if (instruction.opcode == O::StoreLoadLocal) {
            kind = NodeKind::Store;
            label = "store local " + instruction.symbol;
            secondary_kind = NodeKind::Load;
            secondary_label = "load local " + instruction.secondary_symbol;
        }
        instruction_nodes.push_back(graph.addNode(
            kind, std::move(label), instruction.offset, code));
        if (!secondary_label.empty())
            secondary_instruction_nodes[index] = graph.addNode(
                secondary_kind, std::move(secondary_label),
                instruction.offset, code);
        if (instruction.implicit_constant)
            implicit_constants[instruction_nodes.size() - 1] =
                graph.addNode(NodeKind::Constant, "implicit constant", instruction.offset, code);
    }

    std::unordered_map<std::string, NodeId> local_inputs;
    std::unordered_map<std::string, NodeId> global_inputs;
    std::vector<State> incoming(cfg.blocks().size() + 1);
    incoming[1].initialized = true;
    // Seed every read namespace at entry. A store kills this value on paths where
    // it executes; paths without the store retain the external/unbound input.
    const auto seed_input = [&](const std::string& name, bool local, std::uint32_t offset) {
        auto& inputs = local ? local_inputs : global_inputs;
        if (name.empty() || inputs.count(name)) return;
        const auto input = graph.addNode(NodeKind::Input,
            std::string(local ? "local " : "global ") + name, offset, code);
        inputs.emplace(name, input);
        (local ? incoming[1].locals : incoming[1].globals)[name] = {input};
    };
    for (const auto& parameter : parameter_names)
        seed_input(parameter, true, program.front().offset);
    for (const auto& instruction : program) {
        using O = bytecode::SemanticOpcode;
        if (instruction.opcode == O::LoadLocal)
            seed_input(instruction.symbol, true, instruction.offset);
        else if (instruction.opcode == O::LoadGlobal)
            seed_input(instruction.symbol, false, instruction.offset);
        else if (instruction.opcode == O::LoadLocalPair) {
            seed_input(instruction.symbol, true, instruction.offset);
            seed_input(instruction.secondary_symbol, true, instruction.offset);
        } else if (instruction.opcode == O::StoreLoadLocal)
            seed_input(instruction.secondary_symbol, true, instruction.offset);
    }
    std::deque<cfg::BlockId> worklist{1};
    std::vector<bool> queued(cfg.blocks().size() + 1, false);
    queued[1] = true;
    std::unordered_map<cfg::BlockId, std::vector<NodeId>> exception_inputs;
    std::unordered_map<cfg::BlockId, std::vector<NodeId>> protected_stack_inputs;

    const auto connect = [&](const ValueSet& values, NodeId target, EdgeKind kind) {
        for (const auto source : values) graph.addEdge(source, target, kind);
    };
    const auto record_call_boundary = [&result](
        std::size_t instruction_index, NodeId call,
        const std::vector<std::vector<NodeId>>& actual_arguments) {
        const auto found = std::find_if(
            result.call_boundaries.begin(), result.call_boundaries.end(),
            [instruction_index](const auto& boundary) {
                return boundary.instruction_index == instruction_index;
            });
        if (found == result.call_boundaries.end()) {
            result.call_boundaries.push_back(
                {instruction_index, call, actual_arguments});
            return;
        }
        if (found->call != call ||
            found->actual_arguments.size() != actual_arguments.size())
            throw std::logic_error(
                "DDG call boundary changed during fixed-point iteration");
        for (std::size_t argument = 0;
             argument < actual_arguments.size(); ++argument)
            for (const auto definition : actual_arguments[argument])
                if (std::find(found->actual_arguments[argument].begin(),
                              found->actual_arguments[argument].end(),
                              definition) ==
                    found->actual_arguments[argument].end())
                    found->actual_arguments[argument].push_back(definition);
    };
    const auto record_heap_access = [&result](
        std::size_t instruction_index, cfg::BlockId block, NodeId node,
        DDGAccessKind kind, DDGHeapLocationKind location,
        const std::string& field, const ValueSet& bases) {
        const auto found = std::find_if(
            result.heap_accesses.begin(), result.heap_accesses.end(),
            [instruction_index, kind](const auto& access) {
                return access.instruction_index == instruction_index &&
                       access.kind == kind;
            });
        if (found == result.heap_accesses.end()) {
            result.heap_accesses.push_back({
                instruction_index, block, node, kind, location, field,
                {bases.begin(), bases.end()},
            });
            return;
        }
        if (found->block != block || found->node != node ||
            found->location != location || found->field != field)
            throw std::logic_error(
                "DDG heap access changed during fixed-point iteration");
        for (const auto base : bases)
            if (std::find(found->bases.begin(), found->bases.end(), base) ==
                found->bases.end())
                found->bases.push_back(base);
    };
    const auto record_global_access = [&result](
        NodeId node, DDGAccessKind kind, const std::string& name) {
        const auto found = std::find_if(
            result.global_accesses.begin(), result.global_accesses.end(),
            [&](const auto& access) {
                return access.node == node && access.kind == kind &&
                       access.name == name;
            });
        if (found == result.global_accesses.end())
            result.global_accesses.push_back({node, kind, name});
    };
    while (!worklist.empty()) {
        const auto block_id = worklist.front();
        worklist.pop_front();
        queued[block_id] = false;
        State state = incoming[block_id];
        for (const auto index : cfg.block(block_id).instruction_indices) {
            const auto& instruction = program[index];
            const auto current = instruction_nodes[index];
            using O = bytecode::SemanticOpcode;
            switch (instruction.opcode) {
                case O::Nop:
                case O::PrepareKeywordCall:
                case O::Branch:
                    break;
                case O::LoadConst:
                case O::LoadLiteral:
                    state.stack.push_back({current});
                    break;
                case O::CallProtocolMarker:
                    state.stack.push_back({});
                    break;
                case O::Generic:
                case O::BuildCollection:
                case O::UnpackCollection:
                case O::Yield:
                case O::Suspend: {
                    requireStack(state, instruction.stack_peek_count,
                                 instruction.offset, "generic peek");
                    requireStack(state, instruction.stack_input_count,
                                 instruction.offset, "generic input");
                    for (std::uint32_t peeked = 0; peeked < instruction.stack_peek_count; ++peeked) {
                        connect(state.stack[state.stack.size() - 1U - peeked], current,
                                EdgeKind::Operand);
                    }
                    for (std::uint32_t input = 0; input < instruction.stack_input_count; ++input)
                        connect(pop(state, instruction.offset), current, EdgeKind::Operand);
                    for (std::uint32_t output = 0;
                         output < instruction.stack_output_count; ++output) {
                        if (instruction.stack_output_count ==
                            kSingleStackOutput) {
                            state.stack.push_back({current});
                            continue;
                        }
                        const auto output_node = graph.addNode(
                            NodeKind::Operation,
                            std::string(kOperationResultLabel) +
                                std::to_string(
                                    instruction.stack_output_count - output -
                                    kStackOutputIndexBias),
                            instruction.offset, code);
                        graph.addEdge(
                            current, output_node, EdgeKind::Result);
                        state.stack.push_back({output_node});
                    }
                    break;
                }
                case O::StackCopy: {
                    if (instruction.operand == 0U)
                        throw std::runtime_error(
                            "DDG stack-copy depth zero at bytecode offset " +
                            std::to_string(instruction.offset));
                    requireStack(state, instruction.operand,
                                 instruction.offset, "stack copy");
                    const auto& source = state.stack[state.stack.size() - instruction.operand];
                    connect(source, current, EdgeKind::Operand);
                    state.stack.push_back({current});
                    break;
                }
                case O::StackSwap:
                    if (instruction.operand == 0U)
                        throw std::runtime_error(
                            "DDG stack-swap depth zero at bytecode offset " +
                            std::to_string(instruction.offset));
                    requireStack(state, instruction.operand,
                                 instruction.offset, "stack swap");
                    std::swap(state.stack.back(),
                              state.stack[state.stack.size() - instruction.operand]);
                    break;
                case O::StackRotate:
                    if (instruction.operand == 0U)
                        throw std::runtime_error(
                            "DDG stack-rotate depth zero at bytecode offset " +
                            std::to_string(instruction.offset));
                    requireStack(state, instruction.operand,
                                 instruction.offset, "stack rotate");
                    std::rotate(state.stack.end() - instruction.operand,
                                state.stack.end() - 1, state.stack.end());
                    break;
                case O::LoadElement: {
                    const auto key = pop(state, instruction.offset);
                    const auto base = pop(state, instruction.offset);
                    connect(key, current, EdgeKind::Address);
                    connect(base, current, EdgeKind::Address);
                    record_heap_access(
                        index, block_id, current, DDGAccessKind::Load,
                        DDGHeapLocationKind::Element, {}, base);
                    state.stack.push_back({current});
                    break;
                }
                case O::StoreElement: {
                    const auto key = pop(state, instruction.offset);
                    const auto base = pop(state, instruction.offset);
                    const auto value = pop(state, instruction.offset);
                    connect(key, current, EdgeKind::Address);
                    connect(base, current, EdgeKind::Address);
                    connect(value, current, EdgeKind::Operand);
                    record_heap_access(
                        index, block_id, current, DDGAccessKind::Store,
                        DDGHeapLocationKind::Element, {}, base);
                    break;
                }
                case O::StoreCollectionElement: {
                    for (std::uint32_t input = 0;
                         input < instruction.stack_input_count; ++input)
                        connect(pop(state, instruction.offset), current,
                                EdgeKind::Operand);
                    if (instruction.operand == 0 ||
                        instruction.operand > state.stack.size())
                        throw std::runtime_error(
                            "DDG collection-update depth is out of range at bytecode offset " +
                            std::to_string(instruction.offset));
                    auto& collection =
                        state.stack[state.stack.size() - instruction.operand];
                    connect(collection, current, EdgeKind::Operand);
                    record_heap_access(
                        index, block_id, current, DDGAccessKind::Store,
                        DDGHeapLocationKind::Element, {}, collection);
                    collection = {current};
                    break;
                }
                case O::DeleteElement:
                    for (std::uint32_t input = 0; input < 2; ++input)
                        connect(pop(state, instruction.offset), current, EdgeKind::Operand);
                    break;
                case O::ImportModule:
                    for (std::uint32_t discarded = 0;
                         discarded < instruction.discarded_stack_values; ++discarded)
                        connect(pop(state, instruction.offset), current, EdgeKind::Operand);
                    state.stack.push_back({current});
                    break;
                case O::ImportAttribute: {
                    if (state.stack.empty()) pop(state, instruction.offset);
                    connect(state.stack.back(), current, EdgeKind::Operand);
                    state.stack.push_back({current});
                    break;
                }
                case O::EnterContext: {
                    const auto manager = pop(state, instruction.offset);
                    connect(manager, current, EdgeKind::Operand);
                    // One node represents the two protocol results in the
                    // DDG; PTA retains their distinct object identities.
                    state.stack.push_back({current});
                    state.stack.push_back({current});
                    break;
                }
                case O::LoadLocal:
                case O::LoadGlobal: {
                    const bool local = instruction.opcode == O::LoadLocal;
                    auto& variables = local ? state.locals : state.globals;
                    const auto found = variables.find(instruction.symbol);
                    if (found == variables.end() || found->second.empty()) {
                        const auto& inputs = local ? local_inputs : global_inputs;
                        const auto input = inputs.find(instruction.symbol);
                        if (input == inputs.end())
                            throw std::logic_error("DDG input was not seeded for loaded variable");
                        connect({input->second}, current, EdgeKind::DefUse);
                    } else {
                        connect(found->second, current, EdgeKind::DefUse);
                    }
                    if (instruction.push_call_protocol_marker) state.stack.push_back({});
                    state.stack.push_back({current});
                    if (local && instruction.clear_local_after_load)
                        state.locals.erase(instruction.symbol);
                    if (!local)
                        record_global_access(
                            current, DDGAccessKind::Load,
                            instruction.symbol);
                    break;
                }
                case O::StoreLocal:
                case O::StoreGlobal: {
                    const auto value = pop(state, instruction.offset);
                    connect(value, current, EdgeKind::Operand);
                    auto& variables = instruction.opcode == O::StoreLocal ? state.locals : state.globals;
                    variables[instruction.symbol] = {current};
                    if (instruction.opcode == O::StoreGlobal)
                        record_global_access(
                            current, DDGAccessKind::Store,
                            instruction.symbol);
                    break;
                }
                case O::DeleteLocal:
                    state.locals.erase(instruction.symbol);
                    break;
                case O::DeleteGlobal:
                    state.globals.erase(instruction.symbol);
                    break;
                case O::LoadLocalPair: {
                    const std::array<std::pair<const std::string*, NodeId>, 2U>
                        outputs{{
                            {&instruction.symbol, current},
                            {&instruction.secondary_symbol,
                             secondary_instruction_nodes[index]},
                        }};
                    for (const auto& [name, output] : outputs) {
                        const auto found = state.locals.find(*name);
                        if (found == state.locals.end() || found->second.empty()) {
                            const auto input = local_inputs.find(*name);
                            if (input == local_inputs.end())
                                throw std::logic_error("DDG input was not seeded for loaded variable");
                            connect({input->second}, output, EdgeKind::DefUse);
                        } else {
                            connect(found->second, output, EdgeKind::DefUse);
                        }
                        state.stack.push_back({output});
                    }
                    break;
                }
                case O::StoreLocalPair: {
                    const std::array<std::pair<const std::string*, NodeId>, 2U>
                        outputs{{
                            {&instruction.symbol, current},
                            {&instruction.secondary_symbol,
                             secondary_instruction_nodes[index]},
                        }};
                    for (const auto& [name, output] : outputs) {
                        const auto value = pop(state, instruction.offset);
                        connect(value, output, EdgeKind::Operand);
                        state.locals[*name] = {output};
                    }
                    break;
                }
                case O::StoreLoadLocal: {
                    const auto stored = pop(state, instruction.offset);
                    connect(stored, current, EdgeKind::Operand);
                    state.locals[instruction.symbol] = {current};
                    const auto loaded = secondary_instruction_nodes[index];
                    const auto found = state.locals.find(instruction.secondary_symbol);
                    if (found == state.locals.end() || found->second.empty()) {
                        const auto input = local_inputs.find(instruction.secondary_symbol);
                        if (input == local_inputs.end())
                            throw std::logic_error("DDG input was not seeded for loaded variable");
                        connect({input->second}, loaded, EdgeKind::DefUse);
                    } else {
                        connect(found->second, loaded, EdgeKind::DefUse);
                    }
                    state.stack.push_back({loaded});
                    break;
                }
                case O::LoadAttribute: {
                    const auto base = pop(state, instruction.offset);
                    for (std::uint32_t discarded = 0;
                         discarded < instruction.discarded_stack_values;
                         ++discarded)
                        pop(state, instruction.offset);
                    connect(base, current, EdgeKind::Address);
                    record_heap_access(
                        index, block_id, current, DDGAccessKind::Load,
                        DDGHeapLocationKind::Attribute, instruction.symbol,
                        base);
                    state.stack.push_back({current});
                    if (instruction.push_call_receiver) state.stack.push_back(base);
                    break;
                }
                case O::StoreAttribute: {
                    const auto object = pop(state, instruction.offset);
                    const auto value = pop(state, instruction.offset);
                    connect(object, current, EdgeKind::Address);
                    connect(value, current, EdgeKind::Operand);
                    record_heap_access(
                        index, block_id, current, DDGAccessKind::Store,
                        DDGHeapLocationKind::Attribute, instruction.symbol,
                        object);
                    break;
                }
                case O::CreateFunction: {
                    for (std::uint32_t discarded = 0;
                         discarded < instruction.discarded_stack_values; ++discarded)
                        pop(state, instruction.offset);
                    const auto code_value = pop(state, instruction.offset);
                    connect(code_value, current, EdgeKind::Operand);
                    for (std::uint32_t auxiliary = 0;
                         auxiliary < instruction.auxiliary_input_count; ++auxiliary)
                        connect(pop(state, instruction.offset), current, EdgeKind::Operand);
                    state.stack.push_back({current});
                    break;
                }
                case O::SetFunctionAttribute: {
                    const auto functions = pop(state, instruction.offset);
                    const auto attributes = pop(state, instruction.offset);
                    connect(attributes, current, EdgeKind::Operand);
                    connect(functions, current, EdgeKind::DefUse);
                    state.stack.push_back({current});
                    break;
                }
                case O::Call: {
                    for (std::uint32_t metadata = 0;
                         metadata < instruction.discarded_stack_values; ++metadata)
                        pop(state, instruction.offset);
                    std::vector<std::vector<NodeId>> actual_arguments;
                    actual_arguments.reserve(instruction.operand + 1U);
                    for (std::uint32_t argument = 0;
                         argument < instruction.operand; ++argument) {
                        const auto values = pop(state, instruction.offset);
                        connect(values, current, EdgeKind::Operand);
                        actual_arguments.emplace_back(
                            values.begin(), values.end());
                    }
                    std::reverse(
                        actual_arguments.begin(), actual_arguments.end());
                    if (instruction.call_protocol_input_count ==
                        kSingleCallProtocolInput) {
                        connect(pop(state, instruction.offset), current,
                                EdgeKind::Callee);
                    } else if (instruction.call_protocol_input_count ==
                               kDualCallProtocolInputs) {
                        const auto protocol_top = pop(state, instruction.offset);
                        const auto protocol_bottom = pop(
                            state, instruction.offset);
                        if (protocol_top.empty()) {
                            connect(protocol_bottom, current,
                                    EdgeKind::Callee);
                        } else if (protocol_bottom.empty()) {
                            connect(protocol_top, current, EdgeKind::Callee);
                        } else {
                            connect(protocol_bottom, current,
                                    EdgeKind::Callee);
                            connect(protocol_top, current,
                                    EdgeKind::Operand);
                            actual_arguments.insert(
                                actual_arguments.begin(),
                                std::vector<NodeId>(protocol_top.begin(),
                                                    protocol_top.end()));
                        }
                    } else {
                        for (std::uint32_t protocol = 0;
                             protocol < instruction.call_protocol_input_count;
                             ++protocol)
                            connect(pop(state, instruction.offset), current,
                                    EdgeKind::Operand);
                    }
                    record_call_boundary(index, current, actual_arguments);
                    state.stack.push_back({current});
                    break;
                }
                case O::Return:
                    if (instruction.implicit_constant)
                        graph.addEdge(implicit_constants[index], current, EdgeKind::ReturnValue);
                    else
                        connect(pop(state, instruction.offset), current, EdgeKind::ReturnValue);
                    if (std::find(result.return_values.begin(),
                                  result.return_values.end(), current) ==
                        result.return_values.end())
                        result.return_values.push_back(current);
                    break;
                case O::Raise:
                    for (std::uint32_t input = 0; input < instruction.stack_input_count; ++input)
                        connect(pop(state, instruction.offset), current, EdgeKind::Operand);
                    break;
                case O::Pop:
                    connect(pop(state, instruction.offset), current, EdgeKind::Operand);
                    break;
                case O::ConditionalBranch:
                    requireStack(state,
                                 std::max({instruction.stack_peek_count,
                                           bytecode::conditionalStackInputs(instruction, false),
                                           bytecode::conditionalStackInputs(instruction, true)}),
                                 instruction.offset, "conditional branch");
                    for (std::uint32_t depth = 0;
                         depth < std::max({instruction.stack_peek_count,
                                           bytecode::conditionalStackInputs(instruction, false),
                                           bytecode::conditionalStackInputs(instruction, true)}); ++depth) {
                        connect(state.stack[state.stack.size() - 1U - depth], current,
                                EdgeKind::Operand);
                    }
                    break;
                case O::Unsupported:
                    throw std::runtime_error("unsupported semantic instruction at bytecode offset " +
                                             std::to_string(instruction.offset));
            }
        }
        const auto& last = program[cfg.block(block_id).instruction_indices.back()];
        for (const auto& edge : cfg.outgoingEdges(block_id)) {
            State outgoing = state;
            if (edge.kind == cfg::EdgeKind::Exception) {
                while (outgoing.stack.size() < edge.stack_depth) {
                    auto& values = protected_stack_inputs[edge.target];
                    const auto index = outgoing.stack.size();
                    while (values.size() <= index)
                        values.push_back(graph.addNode(NodeKind::Input,
                        "unknown protected-stack value",
                            cfg.block(edge.target).start_offset, code));
                    outgoing.stack.push_back({values[index]});
                }
                outgoing.stack.resize(edge.stack_depth);
                auto& values = exception_inputs[edge.target];
                while (values.size() < edge.exception_stack_items) {
                    values.push_back(graph.addNode(NodeKind::Input, "exception state",
                                                   cfg.block(edge.target).start_offset, code));
                }
                const auto throwing_instruction = instruction_nodes[
                    cfg.block(block_id).instruction_indices.back()];
                for (std::uint32_t item = 0; item < edge.exception_stack_items; ++item) {
                    graph.addEdge(throwing_instruction, values[item], EdgeKind::Result);
                    outgoing.stack.push_back({values[item]});
                }
            } else if (last.opcode == bytecode::SemanticOpcode::ConditionalBranch) {
                const bool jump_edge = edge.kind == (last.jump_on_true
                    ? cfg::EdgeKind::BranchTrue : cfg::EdgeKind::BranchFalse);
                const auto inputs = bytecode::conditionalStackInputs(last, jump_edge);
                const auto outputs = bytecode::conditionalStackOutputs(last, jump_edge);
                for (std::uint32_t input = 0; input < inputs; ++input)
                    pop(outgoing, last.offset);
                const auto branch_node = instruction_nodes[
                    cfg.block(block_id).instruction_indices.back()];
                for (std::uint32_t output = 0; output < outputs; ++output)
                    outgoing.stack.push_back({branch_node});
            }
            if (mergeState(incoming[edge.target], outgoing,
                           cfg.block(block_id).start_offset,
                           cfg.block(edge.target).start_offset) && !queued[edge.target]) {
                worklist.push_back(edge.target);
                queued[edge.target] = true;
            }
        }
    }
    result.formal_parameters.reserve(parameter_names.size());
    for (const auto& parameter : parameter_names) {
        const auto input = local_inputs.find(parameter);
        if (input == local_inputs.end())
            throw std::logic_error("DDG formal parameter input was not seeded");
        result.formal_parameters.push_back(input->second);
    }
    return result;
}

}  // namespace cpygraph::ddg
