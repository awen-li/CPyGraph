#include "bytecode/adapters/table_adapter.h"
#include "bytecode/adapters/binary_operations.h"
#include "bytecode/adapters/comparison_operations.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cpygraph::bytecode {
namespace {

constexpr std::uint32_t kCodeUnitSize = 2U;

std::uint32_t checkedAdd(std::uint32_t first, std::uint32_t second,
                         const char* message) {
    if (first > std::numeric_limits<std::uint32_t>::max() - second)
        throw std::overflow_error(message);
    return first + second;
}

std::uint32_t checkedMultiply(std::uint32_t value, std::uint32_t factor,
                              const char* message) {
    if (factor != 0U &&
        value > std::numeric_limits<std::uint32_t>::max() / factor)
        throw std::overflow_error(message);
    return value * factor;
}

std::uint32_t codeUnitOffset(std::uint32_t units) {
    return checkedMultiply(units, kCodeUnitSize,
                           "bytecode jump displacement exceeds 32 bits");
}

void setBinaryProtocol(SemanticInstruction& instruction,
                       PythonProtocolOperation operation,
                       PythonSpecialMethod direct,
                       PythonSpecialMethod reflected,
                       PythonSpecialMethod inplace = PythonSpecialMethod::Count) {
    instruction.protocol_operation = operation;
    if (inplace != PythonSpecialMethod::Count)
        instruction.protocol_methods |= pythonMethod(inplace);
    instruction.protocol_methods |= pythonMethod(direct) | pythonMethod(reflected);
}

void annotateBinaryProtocol(SemanticInstruction& instruction) {
    using P = PythonProtocolOperation;
    using M = PythonSpecialMethod;
    switch (instruction.operand) {
        case CPYGRAPH_BINARY_OP_ADD:
            setBinaryProtocol(instruction, P::Add, M::Add, M::ReflectedAdd); break;
        case CPYGRAPH_BINARY_OP_AND:
            setBinaryProtocol(instruction, P::And, M::And, M::ReflectedAnd); break;
        case CPYGRAPH_BINARY_OP_FLOOR_DIVIDE:
            setBinaryProtocol(instruction, P::FloorDivide, M::FloorDivide, M::ReflectedFloorDivide); break;
        case CPYGRAPH_BINARY_OP_LSHIFT:
            setBinaryProtocol(instruction, P::LeftShift, M::LeftShift, M::ReflectedLeftShift); break;
        case CPYGRAPH_BINARY_OP_MATRIX_MULTIPLY:
            setBinaryProtocol(instruction, P::MatrixMultiply, M::MatrixMultiply, M::ReflectedMatrixMultiply); break;
        case CPYGRAPH_BINARY_OP_MULTIPLY:
            setBinaryProtocol(instruction, P::Multiply, M::Multiply, M::ReflectedMultiply); break;
        case CPYGRAPH_BINARY_OP_REMAINDER:
            setBinaryProtocol(instruction, P::Remainder, M::Remainder, M::ReflectedRemainder); break;
        case CPYGRAPH_BINARY_OP_OR:
            setBinaryProtocol(instruction, P::Or, M::Or, M::ReflectedOr); break;
        case CPYGRAPH_BINARY_OP_POWER:
            setBinaryProtocol(instruction, P::Power, M::Power, M::ReflectedPower); break;
        case CPYGRAPH_BINARY_OP_RSHIFT:
            setBinaryProtocol(instruction, P::RightShift, M::RightShift, M::ReflectedRightShift); break;
        case CPYGRAPH_BINARY_OP_SUBTRACT:
            setBinaryProtocol(instruction, P::Subtract, M::Subtract, M::ReflectedSubtract); break;
        case CPYGRAPH_BINARY_OP_TRUE_DIVIDE:
            setBinaryProtocol(instruction, P::TrueDivide, M::TrueDivide, M::ReflectedTrueDivide); break;
        case CPYGRAPH_BINARY_OP_XOR:
            setBinaryProtocol(instruction, P::Xor, M::Xor, M::ReflectedXor); break;
        case CPYGRAPH_BINARY_OP_INPLACE_ADD:
            setBinaryProtocol(instruction, P::InplaceAdd, M::Add, M::ReflectedAdd, M::InplaceAdd); break;
        case CPYGRAPH_BINARY_OP_INPLACE_AND:
            setBinaryProtocol(instruction, P::InplaceAnd, M::And, M::ReflectedAnd, M::InplaceAnd); break;
        case CPYGRAPH_BINARY_OP_INPLACE_FLOOR_DIVIDE:
            setBinaryProtocol(instruction, P::InplaceFloorDivide, M::FloorDivide, M::ReflectedFloorDivide, M::InplaceFloorDivide); break;
        case CPYGRAPH_BINARY_OP_INPLACE_LSHIFT:
            setBinaryProtocol(instruction, P::InplaceLeftShift, M::LeftShift, M::ReflectedLeftShift, M::InplaceLeftShift); break;
        case CPYGRAPH_BINARY_OP_INPLACE_MATRIX_MULTIPLY:
            setBinaryProtocol(instruction, P::InplaceMatrixMultiply, M::MatrixMultiply, M::ReflectedMatrixMultiply, M::InplaceMatrixMultiply); break;
        case CPYGRAPH_BINARY_OP_INPLACE_MULTIPLY:
            setBinaryProtocol(instruction, P::InplaceMultiply, M::Multiply, M::ReflectedMultiply, M::InplaceMultiply); break;
        case CPYGRAPH_BINARY_OP_INPLACE_REMAINDER:
            setBinaryProtocol(instruction, P::InplaceRemainder, M::Remainder, M::ReflectedRemainder, M::InplaceRemainder); break;
        case CPYGRAPH_BINARY_OP_INPLACE_OR:
            setBinaryProtocol(instruction, P::InplaceOr, M::Or, M::ReflectedOr, M::InplaceOr); break;
        case CPYGRAPH_BINARY_OP_INPLACE_POWER:
            setBinaryProtocol(instruction, P::InplacePower, M::Power, M::ReflectedPower, M::InplacePower); break;
        case CPYGRAPH_BINARY_OP_INPLACE_RSHIFT:
            setBinaryProtocol(instruction, P::InplaceRightShift, M::RightShift, M::ReflectedRightShift, M::InplaceRightShift); break;
        case CPYGRAPH_BINARY_OP_INPLACE_SUBTRACT:
            setBinaryProtocol(instruction, P::InplaceSubtract, M::Subtract, M::ReflectedSubtract, M::InplaceSubtract); break;
        case CPYGRAPH_BINARY_OP_INPLACE_TRUE_DIVIDE:
            setBinaryProtocol(instruction, P::InplaceTrueDivide, M::TrueDivide, M::ReflectedTrueDivide, M::InplaceTrueDivide); break;
        case CPYGRAPH_BINARY_OP_INPLACE_XOR:
            setBinaryProtocol(instruction, P::InplaceXor, M::Xor, M::ReflectedXor, M::InplaceXor); break;
        case CPYGRAPH_BINARY_OP_SUBSCRIPT:
            instruction.protocol_operation = P::ItemLoad;
            instruction.protocol_methods = pythonMethod(M::GetItem);
            break;
    }
}

void annotateComparisonProtocol(SemanticInstruction& instruction) {
    using P = PythonProtocolOperation;
    using M = PythonSpecialMethod;
    instruction.protocol_operation = P::Comparison;
    switch (instruction.operand) {
        case CPYGRAPH_COMPARE_LESS:
            instruction.protocol_methods =
                pythonMethod(M::Less) | pythonMethod(M::Greater);
            break;
        case CPYGRAPH_COMPARE_LESS_EQUAL:
            instruction.protocol_methods =
                pythonMethod(M::LessEqual) | pythonMethod(M::GreaterEqual);
            break;
        case CPYGRAPH_COMPARE_EQUAL:
            instruction.protocol_methods = pythonMethod(M::Equal);
            break;
        case CPYGRAPH_COMPARE_NOT_EQUAL:
            instruction.protocol_methods = pythonMethod(M::NotEqual);
            break;
        case CPYGRAPH_COMPARE_GREATER:
            instruction.protocol_methods =
                pythonMethod(M::Greater) | pythonMethod(M::Less);
            break;
        case CPYGRAPH_COMPARE_GREATER_EQUAL:
            instruction.protocol_methods =
                pythonMethod(M::GreaterEqual) | pythonMethod(M::LessEqual);
            break;
        default:
            instruction.protocol_operation = P::DynamicProtocol;
            break;
    }
}

}  // namespace

TableDrivenCPythonAdapter::TableDrivenCPythonAdapter(
    std::unordered_map<std::uint8_t, OpcodeSemantics> table, std::uint8_t extended_arg,
    bool omit_caches, std::uint8_t cache_opcode, bool encoded_exception_table,
    AdapterFeatures features)
    : table_(std::move(table)), extended_arg_(extended_arg), omit_caches_(omit_caches),
      cache_opcode_(cache_opcode), encoded_exception_table_(encoded_exception_table),
      features_(std::move(features)) {}

WordcodeParser TableDrivenCPythonAdapter::parser() const {
    return WordcodeParser(extended_arg_, cache_opcode_, omit_caches_);
}

std::vector<ExceptionRegion> TableDrivenCPythonAdapter::exceptionRegions(
    const std::vector<std::uint8_t>&, const CodeMetadata& metadata) const {
    return encoded_exception_table_ ? decodeExceptionTable(metadata.exception_table)
                                    : std::vector<ExceptionRegion>{};
}

std::uint32_t TableDrivenCPythonAdapter::nameIndex(
    std::uint8_t opcode, std::uint32_t argument) const noexcept {
    if (features_.method_super_attribute &&
        opcode == *features_.method_super_attribute)
        return argument >> features_.method_super_attribute_name_shift;
    if ((features_.flagged_load_global && opcode == *features_.flagged_load_global) ||
        (features_.flagged_load_attribute && opcode == *features_.flagged_load_attribute))
        return argument >> 1U;
    return argument;
}

SemanticInstruction TableDrivenCPythonAdapter::lift(const RawInstruction& instruction,
                                                     const CodeMetadata& metadata) const {
    SemanticInstruction result;
    result.offset = instruction.offset;
    result.opcode = SemanticOpcode::Unsupported;
    result.operand = instruction.argument;
    const auto found = table_.find(instruction.opcode);
    if (found == table_.end()) return result;
    const auto& semantics = found->second;
    result.opcode = semantics.opcode;
    result.lexical_access = semantics.lexical_access;
    result.jump_on_true = semantics.jump_on_true;
    result.implicit_constant = semantics.implicit_constant;
    result.positional_collection = semantics.positional_collection;
    std::uint32_t index = instruction.argument;
    switch (semantics.operand_source) {
        case OperandSource::Name:
            index = nameIndex(instruction.opcode, instruction.argument);
            if (index >= metadata.names.size()) throw std::out_of_range("bytecode name index is out of range");
            result.symbol = metadata.names[index];
            result.operand = index;
            break;
        case OperandSource::Local:
            if (index >= metadata.locals.size()) throw std::out_of_range("bytecode local index is out of range");
            result.symbol = metadata.locals[index];
            result.operand = index;
            break;
        case OperandSource::Deref:
            if (index >= metadata.deref_names.size() || metadata.deref_names[index].empty())
                throw std::out_of_range("bytecode deref index is out of range");
            result.symbol = metadata.deref_names[index];
            result.operand = index;
            break;
        case OperandSource::Constant:
            if (index >= metadata.constant_count) throw std::out_of_range("bytecode constant index is out of range");
            result.operand = index;
            break;
        case OperandSource::None:
        case OperandSource::Count:
            break;
    }
    if (features_.cell_reference_local_opcode == instruction.opcode &&
        (std::find(metadata.cell_names.begin(), metadata.cell_names.end(),
                   result.symbol) != metadata.cell_names.end() ||
         std::find(metadata.free_names.begin(), metadata.free_names.end(),
                   result.symbol) != metadata.free_names.end()))
        result.lexical_access = LexicalAccessKind::CellReference;
    const auto jump_base = checkedAdd(
        checkedAdd(instruction.offset, instruction.prefix_size,
                   "bytecode jump base exceeds 32 bits"),
        kCodeUnitSize, "bytecode jump base exceeds 32 bits");
    switch (semantics.jump_kind) {
        case JumpKind::Absolute:
            result.jump_target = codeUnitOffset(instruction.argument);
            break;
        case JumpKind::RelativeForward:
            result.jump_target = checkedAdd(
                jump_base, codeUnitOffset(instruction.argument),
                "forward bytecode jump target exceeds 32 bits");
            break;
        case JumpKind::RelativeBackward: {
            const auto displacement = codeUnitOffset(instruction.argument);
            if (displacement > jump_base)
                throw std::overflow_error(
                    "backward bytecode jump target precedes offset zero");
            result.jump_target = jump_base - displacement;
            break;
        }
        case JumpKind::None: break;
    }
    if (semantics.jump_kind != JumpKind::None) result.operand = 0;
    result.stack_input_count = semantics.stack_inputs;
    result.stack_output_count = semantics.stack_outputs;
    result.stack_peek_count = semantics.stack_peeks;
    result.fresh_result = semantics.fresh_result;
    result.provenance_preserving = semantics.provenance_preserving;
    result.protocol_operation = semantics.protocol_operation;
    result.protocol_methods = semantics.protocol_methods;
    if (features_.binary_op_opcode &&
        instruction.opcode == *features_.binary_op_opcode)
        annotateBinaryProtocol(result);
    if (features_.comparison_opcode &&
        instruction.opcode == *features_.comparison_opcode) {
        result.operand = instruction.argument >>
                         features_.comparison_argument_shift;
        switch (result.operand) {
            case CPYGRAPH_COMPARE_LESS:
                result.comparison_kind = PythonComparisonKind::Less;
                break;
            case CPYGRAPH_COMPARE_LESS_EQUAL:
                result.comparison_kind = PythonComparisonKind::LessEqual;
                break;
            case CPYGRAPH_COMPARE_EQUAL:
                result.comparison_kind = PythonComparisonKind::Equal;
                break;
            case CPYGRAPH_COMPARE_NOT_EQUAL:
                result.comparison_kind = PythonComparisonKind::NotEqual;
                break;
            case CPYGRAPH_COMPARE_GREATER:
                result.comparison_kind = PythonComparisonKind::Greater;
                break;
            case CPYGRAPH_COMPARE_GREATER_EQUAL:
                result.comparison_kind = PythonComparisonKind::GreaterEqual;
                break;
            default: break;
        }
        annotateComparisonProtocol(result);
    }
    if (features_.import_star_intrinsic_opcode &&
        instruction.opcode == *features_.import_star_intrinsic_opcode &&
        instruction.argument == features_.import_star_intrinsic_operand)
        result.protocol_operation = PythonProtocolOperation::Import;
    if (result.opcode == SemanticOpcode::ConditionalBranch)
        conditionalStack(result, 1, 0, 1, 0);
    switch (semantics.stack_effect) {
        case StackEffect::None:
        case StackEffect::Fixed:
            break;
        case StackEffect::CountToOne:
            result.stack_input_count = instruction.argument;
            result.stack_output_count = 1;
            break;
        case StackEffect::MapToOne:
            result.stack_input_count = checkedMultiply(
                instruction.argument, 2U,
                "map construction stack effect exceeds 32 bits");
            result.stack_output_count = 1;
            break;
        case StackEffect::ConstKeyMapToOne:
            result.stack_input_count = checkedAdd(
                instruction.argument, 1U,
                "constant-key map stack effect exceeds 32 bits");
            result.stack_output_count = 1;
            break;
        case StackEffect::Unpack:
            result.stack_input_count = 1;
            result.stack_output_count = instruction.argument;
            break;
        case StackEffect::UnpackEx:
            result.stack_input_count = 1;
            result.stack_output_count = (instruction.argument & 0xffU) +
                                        (instruction.argument >> 8U) + 1U;
            break;
        case StackEffect::FormatValue:
            result.stack_input_count = 1U + ((instruction.argument & 0x04U) != 0U);
            result.stack_output_count = 1;
            break;
        case StackEffect::CallFunctionEx:
            result.stack_input_count = 2U + (instruction.argument & 1U);
            result.stack_output_count = 1;
            break;
        case StackEffect::RaiseArguments:
            result.stack_input_count = instruction.argument;
            result.stack_output_count = 0;
            break;
    }
    if ((result.opcode == SemanticOpcode::LoadConst ||
         result.implicit_constant) &&
        result.operand < metadata.constant_integers.size())
        result.constant_integer = metadata.constant_integers[result.operand];
    if ((result.opcode == SemanticOpcode::LoadConst ||
         result.implicit_constant) &&
        result.operand < metadata.constant_strings.size())
        result.constant_string = metadata.constant_strings[result.operand];
    if ((result.opcode == SemanticOpcode::LoadConst ||
         result.implicit_constant) &&
        result.operand < metadata.constant_kinds.size())
        result.constant_kind = metadata.constant_kinds[result.operand];

    const auto has_opcode = [&](const std::vector<std::uint8_t>& opcodes) {
        return std::find(opcodes.begin(), opcodes.end(), instruction.opcode) != opcodes.end();
    };
    if (result.opcode == SemanticOpcode::Call)
        result.call_protocol_input_count = features_.call_protocol_inputs;
    if (features_.flagged_load_global &&
        instruction.opcode == *features_.flagged_load_global &&
        (instruction.argument & 1U) != 0U)
        result.push_call_protocol_marker = true;
    if ((features_.flagged_load_attribute &&
         instruction.opcode == *features_.flagged_load_attribute &&
         (instruction.argument & 1U) != 0U) ||
        has_opcode(features_.receiver_opcodes))
        result.push_call_receiver = true;
    if (features_.method_super_attribute &&
        instruction.opcode == *features_.method_super_attribute) {
        result.super_attribute = true;
        result.discarded_stack_values = 2U;
        result.push_call_receiver = (instruction.argument & 1U) != 0U;
        result.stack_input_count = 3U;
        result.stack_output_count =
            1U + static_cast<std::uint32_t>(result.push_call_receiver);
    }
    if (has_opcode(features_.clear_local_opcodes)) result.clear_local_after_load = true;
    if (has_opcode(features_.preserve_argument_opcodes)) result.operand = instruction.argument;
    for (const auto& rule : features_.conditional_stack_rules) {
        if (instruction.opcode != rule.opcode) continue;
        conditionalStack(result, rule.fallthrough_inputs, rule.fallthrough_outputs,
                         rule.jump_inputs, rule.jump_outputs, rule.peeks);
        break;
    }
    if (result.jump_target && has_opcode(features_.jump_cache_adjusted_opcodes))
        *result.jump_target = checkedAdd(
            *result.jump_target, kCodeUnitSize,
            "cache-adjusted bytecode jump target exceeds 32 bits");
    if (features_.expanded_call_opcode &&
        instruction.opcode == *features_.expanded_call_opcode) {
        result.operand = features_.expanded_call_operand == ExpandedCallOperand::ArgsAndKeywords
            ? 2U : 1U + (instruction.argument & 1U);
        result.expanded_arguments = true;
    }
    if (features_.keyword_call_opcode &&
        instruction.opcode == *features_.keyword_call_opcode) {
        result.operand = instruction.argument;
        result.discarded_stack_values = 1;
        result.keyword_arguments = true;
    }
    if (has_opcode(features_.combined_local_opcodes)) {
        const auto first = instruction.argument >> 4U;
        const auto second = instruction.argument & 0x0fU;
        if (first >= metadata.deref_names.size() || second >= metadata.deref_names.size() ||
            metadata.deref_names[first].empty() || metadata.deref_names[second].empty())
            throw std::out_of_range("combined local index is out of range");
        result.symbol = metadata.deref_names[first];
        result.secondary_symbol = metadata.deref_names[second];
        result.operand = 0;
    }
    if (result.opcode == SemanticOpcode::CreateFunction &&
        features_.normalize_create_function) {
        if (features_.create_function_flag_inputs) {
            std::uint32_t inputs = 0;
            for (std::uint32_t bit = 1; bit <= 8; bit <<= 1U)
                if ((instruction.argument & bit) != 0U) ++inputs;
            result.auxiliary_input_count = inputs;
        }
        result.discarded_stack_values = features_.create_function_discarded_inputs;
        result.operand = 0;
    }
    if (result.opcode == SemanticOpcode::ImportModule) result.discarded_stack_values = 2;
    return result;
}

}  // namespace cpygraph::bytecode
