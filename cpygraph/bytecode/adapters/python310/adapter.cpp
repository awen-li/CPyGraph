#include "bytecode/adapters/python310/adapter.h"
#include "bytecode/adapters/python310/opcodes.h"

#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace cpygraph::bytecode {
namespace {
using O = SemanticOpcode;
using S = OperandSource;
using J = JumpKind;
using P = PythonProtocolOperation;
using M = PythonSpecialMethod;

OpcodeSemantics binaryProtocol(P operation, M direct, M reflected) {
    return protocolStack(2U, 1U, operation,
                         pythonMethod(direct) | pythonMethod(reflected));
}

OpcodeSemantics inplaceProtocol(P operation, M inplace, M direct, M reflected) {
    return protocolStack(2U, 1U, operation, pythonMethod(inplace) |
        pythonMethod(direct) | pythonMethod(reflected));
}

AdapterFeatures features() {
    AdapterFeatures result;
    result.comparison_opcode = CPYGRAPH_PY310_COMPARE_OP;
    result.preserve_argument_opcodes = {CPYGRAPH_PY310_ROT_N};
    result.conditional_stack_rules = {
        {CPYGRAPH_PY310_FOR_ITER, 0, 1, 1, 0, 1},
        {CPYGRAPH_PY310_JUMP_IF_FALSE_OR_POP, 1, 0, 0, 0, 1},
        {CPYGRAPH_PY310_JUMP_IF_TRUE_OR_POP, 1, 0, 0, 0, 1},
        {CPYGRAPH_PY310_JUMP_IF_NOT_EXC_MATCH, 2, 0, 2, 0, 0},
    };
    result.expanded_call_opcode = CPYGRAPH_PY310_CALL_FUNCTION_EX;
    result.keyword_call_opcode = CPYGRAPH_PY310_CALL_FUNCTION_KW;
    result.normalize_create_function = true;
    result.create_function_flag_inputs = true;
    result.create_function_discarded_inputs = 1;
    return result;
}

std::optional<std::uint32_t> stackAfter(
    std::uint32_t depth, const SemanticInstruction& instruction, bool jump_edge) {
    std::uint32_t inputs = 0;
    std::uint32_t outputs = 0;
    switch (instruction.opcode) {
        case O::Nop:
        case O::PrepareKeywordCall:
        case O::Branch:
        case O::StackSwap:
        case O::StackRotate:
            break;
        case O::LoadConst:
        case O::LoadLiteral:
            outputs = 1;
            break;
        case O::LoadLocal:
        case O::LoadGlobal:
            outputs = 1U + static_cast<std::uint32_t>(
                instruction.push_call_protocol_marker);
            break;
        case O::StoreLocal:
        case O::StoreGlobal:
        case O::Pop:
            inputs = 1;
            break;
        case O::DeleteLocal:
        case O::DeleteGlobal:
            break;
        case O::LoadAttribute:
            inputs = 1;
            outputs = 1U + static_cast<std::uint32_t>(instruction.push_call_receiver);
            break;
        case O::StoreAttribute:
            inputs = 2;
            break;
        case O::ImportModule:
            inputs = instruction.discarded_stack_values;
            outputs = 1;
            break;
        case O::ImportAttribute:
            outputs = 1;  // retain the module and add the imported attribute
            break;
        case O::EnterContext:
            inputs = 1;
            outputs = 2;
            break;
        case O::CreateFunction:
            inputs = 1U + instruction.discarded_stack_values +
                     instruction.auxiliary_input_count;
            outputs = 1;
            break;
        case O::SetFunctionAttribute:
            inputs = 2;
            outputs = 1;
            break;
        case O::Call:
            inputs = instruction.operand + instruction.call_protocol_input_count +
                     instruction.discarded_stack_values;
            outputs = 1;
            break;
        case O::CallProtocolMarker:
        case O::StackCopy:
            outputs = 1;
            break;
        case O::Generic:
        case O::BuildCollection:
        case O::UnpackCollection:
        case O::Yield:
        case O::Suspend:
            inputs = instruction.stack_input_count;
            outputs = instruction.stack_output_count;
            break;
        case O::LoadElement:
            inputs = 2;
            outputs = 1;
            break;
        case O::StoreElement:
            inputs = 3;
            break;
        case O::StoreCollectionElement:
            inputs = instruction.stack_input_count;
            break;
        case O::DeleteElement:
            inputs = 2;
            break;
        case O::LoadLocalPair:
            outputs = 2;
            break;
        case O::StoreLocalPair:
            inputs = 2;
            break;
        case O::StoreLoadLocal:
            inputs = 1;
            outputs = 1;
            break;
        case O::Raise:
            inputs = instruction.stack_input_count;
            break;
        case O::Return:
            inputs = instruction.implicit_constant ? 0U : 1U;
            break;
        case O::ConditionalBranch:
            inputs = conditionalStackInputs(instruction, jump_edge);
            outputs = conditionalStackOutputs(instruction, jump_edge);
            break;
        case O::Unsupported:
            return std::nullopt;
    }
    if (depth < inputs) return std::nullopt;
    return depth - inputs + outputs;
}
}

Python310Adapter::Python310Adapter()
      : TableDrivenCPythonAdapter({
          {CPYGRAPH_PY310_POP_TOP, {O::Pop}},
          {CPYGRAPH_PY310_ROT_TWO, {O::StackRotate}},                 // ROT_TWO
          {CPYGRAPH_PY310_ROT_THREE, {O::StackRotate}},                 // ROT_THREE
          {CPYGRAPH_PY310_DUP_TOP, {O::StackCopy}},                   // DUP_TOP
          {CPYGRAPH_PY310_DUP_TOP_TWO, fixedStack(O::Generic, 0, 2, 2)},  // DUP_TOP_TWO
          {CPYGRAPH_PY310_ROT_FOUR, {O::StackRotate}},                 // ROT_FOUR
          {CPYGRAPH_PY310_NOP, {O::Nop}},
          {CPYGRAPH_PY310_UNARY_POSITIVE, unaryProtocol(P::Positive, M::Positive)},
          {CPYGRAPH_PY310_UNARY_NEGATIVE, unaryProtocol(P::Negative, M::Negative)},
          {CPYGRAPH_PY310_UNARY_NOT, fixedStack(O::Generic, 1, 1)},    // UNARY_NOT
          {CPYGRAPH_PY310_UNARY_INVERT, unaryProtocol(P::Invert, M::Invert)},
          {CPYGRAPH_PY310_BINARY_MATRIX_MULTIPLY, binaryProtocol(P::MatrixMultiply, M::MatrixMultiply, M::ReflectedMatrixMultiply)},
          {CPYGRAPH_PY310_INPLACE_MATRIX_MULTIPLY, inplaceProtocol(P::InplaceMatrixMultiply, M::InplaceMatrixMultiply, M::MatrixMultiply, M::ReflectedMatrixMultiply)},
          {CPYGRAPH_PY310_BINARY_POWER, binaryProtocol(P::Power, M::Power, M::ReflectedPower)},
          {CPYGRAPH_PY310_BINARY_MULTIPLY, binaryProtocol(P::Multiply, M::Multiply, M::ReflectedMultiply)},
          {CPYGRAPH_PY310_BINARY_MODULO, binaryProtocol(P::Remainder, M::Remainder, M::ReflectedRemainder)},
          {CPYGRAPH_PY310_BINARY_ADD, binaryProtocol(P::Add, M::Add, M::ReflectedAdd)},
          {CPYGRAPH_PY310_BINARY_SUBTRACT, binaryProtocol(P::Subtract, M::Subtract, M::ReflectedSubtract)},
          {CPYGRAPH_PY310_BINARY_SUBSCR, {O::LoadElement}},                // BINARY_SUBSCR
          {CPYGRAPH_PY310_BINARY_FLOOR_DIVIDE, binaryProtocol(P::FloorDivide, M::FloorDivide, M::ReflectedFloorDivide)},
          {CPYGRAPH_PY310_BINARY_TRUE_DIVIDE, binaryProtocol(P::TrueDivide, M::TrueDivide, M::ReflectedTrueDivide)},
          {CPYGRAPH_PY310_INPLACE_FLOOR_DIVIDE, inplaceProtocol(P::InplaceFloorDivide, M::InplaceFloorDivide, M::FloorDivide, M::ReflectedFloorDivide)},
          {CPYGRAPH_PY310_INPLACE_TRUE_DIVIDE, inplaceProtocol(P::InplaceTrueDivide, M::InplaceTrueDivide, M::TrueDivide, M::ReflectedTrueDivide)},
          {CPYGRAPH_PY310_GET_LEN, peekProtocol(1U, 1U, P::DynamicProtocol, pythonMethod(M::Len))},
          {CPYGRAPH_PY310_MATCH_MAPPING, fixedStack(O::Generic, 0, 1, 1)}, // MATCH_MAPPING
          {CPYGRAPH_PY310_MATCH_SEQUENCE, fixedStack(O::Generic, 0, 1, 1)}, // MATCH_SEQUENCE
          {CPYGRAPH_PY310_MATCH_KEYS, fixedStack(O::Generic, 0, 2, 2)}, // MATCH_KEYS (values and success flag)
          {CPYGRAPH_PY310_COPY_DICT_WITHOUT_KEYS, fixedStack(O::Generic, 2, 2)},    // COPY_DICT_WITHOUT_KEYS
          {CPYGRAPH_PY310_WITH_EXCEPT_START, fixedStack(O::Generic, 0, 1, 4)}, // WITH_EXCEPT_START
          {CPYGRAPH_PY310_GET_AITER, unaryProtocol(P::Iteration, M::AsyncIter)},
          {CPYGRAPH_PY310_GET_ANEXT, peekProtocol(1U, 1U, P::Iteration, pythonMethod(M::AsyncNext))},
          {CPYGRAPH_PY310_BEFORE_ASYNC_WITH, protocolStack(1U, 2U, P::DynamicProtocol, pythonMethod(M::AsyncEnter) | pythonMethod(M::AsyncExit))},
          // Legacy END_ASYNC_FOR consumes the iterator plus the six values
          // installed by SETUP_FINALLY's exception path.
          {CPYGRAPH_PY310_END_ASYNC_FOR, fixedStack(O::Generic, 7, 0)},    // END_ASYNC_FOR
          {CPYGRAPH_PY310_INPLACE_ADD, inplaceProtocol(P::InplaceAdd, M::InplaceAdd, M::Add, M::ReflectedAdd)},
          {CPYGRAPH_PY310_INPLACE_SUBTRACT, inplaceProtocol(P::InplaceSubtract, M::InplaceSubtract, M::Subtract, M::ReflectedSubtract)},
          {CPYGRAPH_PY310_INPLACE_MULTIPLY, inplaceProtocol(P::InplaceMultiply, M::InplaceMultiply, M::Multiply, M::ReflectedMultiply)},
          {CPYGRAPH_PY310_INPLACE_MODULO, inplaceProtocol(P::InplaceRemainder, M::InplaceRemainder, M::Remainder, M::ReflectedRemainder)},
          {CPYGRAPH_PY310_STORE_SUBSCR, {O::StoreElement}},
          {CPYGRAPH_PY310_DELETE_SUBSCR, {O::DeleteElement}},
          {CPYGRAPH_PY310_BINARY_LSHIFT, binaryProtocol(P::LeftShift, M::LeftShift, M::ReflectedLeftShift)},
          {CPYGRAPH_PY310_BINARY_RSHIFT, binaryProtocol(P::RightShift, M::RightShift, M::ReflectedRightShift)},
          {CPYGRAPH_PY310_BINARY_AND, binaryProtocol(P::And, M::And, M::ReflectedAnd)},
          {CPYGRAPH_PY310_BINARY_XOR, binaryProtocol(P::Xor, M::Xor, M::ReflectedXor)},
          {CPYGRAPH_PY310_BINARY_OR, binaryProtocol(P::Or, M::Or, M::ReflectedOr)},
          {CPYGRAPH_PY310_INPLACE_POWER, inplaceProtocol(P::InplacePower, M::InplacePower, M::Power, M::ReflectedPower)},
          {CPYGRAPH_PY310_GET_ITER, iterationProtocol()},
          {CPYGRAPH_PY310_GET_YIELD_FROM_ITER, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY310_PRINT_EXPR, fixedStack(O::Generic, 1, 0)},    // PRINT_EXPR
          {CPYGRAPH_PY310_LOAD_BUILD_CLASS, fixedStack(O::Generic, 0, 1)},    // LOAD_BUILD_CLASS
          {CPYGRAPH_PY310_YIELD_FROM, fixedStack(O::Yield, 2, 1)},    // YIELD_FROM
          {CPYGRAPH_PY310_GET_AWAITABLE, unaryProtocol(P::DynamicProtocol, M::Await)},
          {CPYGRAPH_PY310_LOAD_ASSERTION_ERROR, {O::LoadLiteral}},                // LOAD_ASSERTION_ERROR
          {CPYGRAPH_PY310_INPLACE_LSHIFT, inplaceProtocol(P::InplaceLeftShift, M::InplaceLeftShift, M::LeftShift, M::ReflectedLeftShift)},
          {CPYGRAPH_PY310_INPLACE_RSHIFT, inplaceProtocol(P::InplaceRightShift, M::InplaceRightShift, M::RightShift, M::ReflectedRightShift)},
          {CPYGRAPH_PY310_INPLACE_AND, inplaceProtocol(P::InplaceAnd, M::InplaceAnd, M::And, M::ReflectedAnd)},
          {CPYGRAPH_PY310_INPLACE_XOR, inplaceProtocol(P::InplaceXor, M::InplaceXor, M::Xor, M::ReflectedXor)},
          {CPYGRAPH_PY310_INPLACE_OR, inplaceProtocol(P::InplaceOr, M::InplaceOr, M::Or, M::ReflectedOr)},
          {CPYGRAPH_PY310_LIST_TO_TUPLE, fixedStack(O::Generic, 1, 1, 0, true)},
          {CPYGRAPH_PY310_RETURN_VALUE, {O::Return}},
          {CPYGRAPH_PY310_IMPORT_STAR, protocolStack(1U, 0U, P::Import, {})},
          {CPYGRAPH_PY310_SETUP_ANNOTATIONS, {O::Nop}},                       // SETUP_ANNOTATIONS
          {CPYGRAPH_PY310_YIELD_VALUE, fixedStack(O::Yield, 1, 1)},    // YIELD_VALUE
          {CPYGRAPH_PY310_POP_BLOCK, {O::Nop}},                       // POP_BLOCK
          {CPYGRAPH_PY310_POP_EXCEPT, fixedStack(O::Generic, 3, 0)},    // POP_EXCEPT restores previous exception triple
          {CPYGRAPH_PY310_STORE_NAME, {O::StoreGlobal, S::Name}},       // STORE_NAME
          {CPYGRAPH_PY310_DELETE_NAME, {O::DeleteGlobal, S::Name}},      // DELETE_NAME
          {CPYGRAPH_PY310_UNPACK_SEQUENCE, collectionUnpack(StackEffect::Unpack)},
          {CPYGRAPH_PY310_FOR_ITER, iterationNextProtocol()},
          {CPYGRAPH_PY310_UNPACK_EX, collectionUnpack(StackEffect::UnpackEx)},
          {CPYGRAPH_PY310_STORE_ATTR, {O::StoreAttribute, S::Name}},
          {CPYGRAPH_PY310_DELETE_ATTR, deleteAttributeProtocol()},
          {CPYGRAPH_PY310_STORE_GLOBAL, {O::StoreGlobal, S::Name}},
          {CPYGRAPH_PY310_DELETE_GLOBAL, {O::DeleteGlobal, S::Name}},
          {CPYGRAPH_PY310_ROT_N, {O::StackRotate}},                // ROT_N
          {CPYGRAPH_PY310_LOAD_CONST, {O::LoadConst, S::Constant}},
          {CPYGRAPH_PY310_LOAD_NAME, {O::LoadGlobal, S::Name}},       // LOAD_NAME
          {CPYGRAPH_PY310_BUILD_TUPLE, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY310_BUILD_LIST, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY310_BUILD_SET, collectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY310_BUILD_MAP, collectionResult(StackEffect::MapToOne)},
          {CPYGRAPH_PY310_LOAD_ATTR, {O::LoadAttribute, S::Name}},
          {CPYGRAPH_PY310_COMPARE_OP, fixedStack(O::Generic, 2, 1)},   // COMPARE_OP
          {CPYGRAPH_PY310_IMPORT_NAME, {O::ImportModule, S::Name}},
          {CPYGRAPH_PY310_IMPORT_FROM, {O::ImportAttribute, S::Name}},
          {CPYGRAPH_PY310_JUMP_FORWARD, {O::Branch, S::None, J::RelativeForward}},
          {CPYGRAPH_PY310_JUMP_IF_FALSE_OR_POP, {O::ConditionalBranch, S::None, J::Absolute, false}},
          {CPYGRAPH_PY310_JUMP_IF_TRUE_OR_POP, {O::ConditionalBranch, S::None, J::Absolute, true}},
          {CPYGRAPH_PY310_JUMP_ABSOLUTE, {O::Branch, S::None, J::Absolute}},
          {CPYGRAPH_PY310_POP_JUMP_IF_FALSE, {O::ConditionalBranch, S::None, J::Absolute, false}},
          {CPYGRAPH_PY310_POP_JUMP_IF_TRUE, {O::ConditionalBranch, S::None, J::Absolute, true}},
          {CPYGRAPH_PY310_LOAD_GLOBAL, {O::LoadGlobal, S::Name}},
          {CPYGRAPH_PY310_IS_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY310_CONTAINS_OP, protocolStack(2U, 1U, P::DynamicProtocol, pythonMethod(M::Contains))},
          {CPYGRAPH_PY310_RERAISE, fixedStack(O::Raise, 3, 0)},     // RERAISE consumes the exception triple
          {CPYGRAPH_PY310_JUMP_IF_NOT_EXC_MATCH, {O::ConditionalBranch, S::None, J::Absolute, false}},
          {CPYGRAPH_PY310_SETUP_FINALLY, {O::Nop}},                       // SETUP_FINALLY
          {CPYGRAPH_PY310_LOAD_FAST, {O::LoadLocal, S::Local}},
          {CPYGRAPH_PY310_STORE_FAST, {O::StoreLocal, S::Local}},
          {CPYGRAPH_PY310_DELETE_FAST, {O::DeleteLocal, S::Local}},
          {CPYGRAPH_PY310_GEN_START, {O::Nop}},                      // GEN_START consumes the implicit send value
          {CPYGRAPH_PY310_RAISE_VARARGS, dynamicStack(O::Raise, StackEffect::RaiseArguments)},
          {CPYGRAPH_PY310_CALL_FUNCTION, {O::Call, S::Count}},
          {CPYGRAPH_PY310_MAKE_FUNCTION, {O::CreateFunction, S::Count}},
          {CPYGRAPH_PY310_BUILD_SLICE, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY310_LOAD_CLOSURE,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellReference)},
          {CPYGRAPH_PY310_LOAD_DEREF,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY310_STORE_DEREF,
           lexicalAccess(O::StoreLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY310_DELETE_DEREF,
           lexicalAccess(O::DeleteLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY310_CALL_FUNCTION_KW, {O::Call, S::Count}},            // CALL_FUNCTION_KW
          {CPYGRAPH_PY310_CALL_FUNCTION_EX, {O::Call, S::Count}},            // CALL_FUNCTION_EX
          // SETUP_WITH replaces the context manager with its __exit__ callable
          // and __enter__ result.  The retained __exit__ value is consumed by
          // the compiler-generated CALL_FUNCTION after the with body.
          {CPYGRAPH_PY310_SETUP_WITH, fixedStack(O::EnterContext, 1, 2)},
          {CPYGRAPH_PY310_LIST_APPEND, collectionStore(1)},             // LIST_APPEND
          {CPYGRAPH_PY310_SET_ADD, collectionStore(1)},             // SET_ADD
          {CPYGRAPH_PY310_MAP_ADD, collectionStore(2)},             // MAP_ADD
          {CPYGRAPH_PY310_LOAD_CLASSDEREF,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY310_MATCH_CLASS, fixedStack(O::Generic, 3, 2)},   // MATCH_CLASS
          {CPYGRAPH_PY310_SETUP_ASYNC_WITH, {O::Nop}},                       // SETUP_ASYNC_WITH
          {CPYGRAPH_PY310_FORMAT_VALUE, dynamicProtocol(StackEffect::FormatValue, P::DynamicProtocol, pythonMethod(M::Format))},
          {CPYGRAPH_PY310_BUILD_CONST_KEY_MAP, collectionResult(StackEffect::ConstKeyMapToOne)},
          {CPYGRAPH_PY310_BUILD_STRING, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY310_LOAD_METHOD, {O::LoadAttribute, S::Name}},    // LOAD_METHOD
          {CPYGRAPH_PY310_CALL_METHOD, {O::Call, S::Count}},            // CALL_METHOD
          {CPYGRAPH_PY310_LIST_EXTEND, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY310_SET_UPDATE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY310_DICT_MERGE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY310_DICT_UPDATE, fixedStack(O::Generic, 1, 0)},
      }, CPYGRAPH_PY310_EXTENDED_ARG, false, CPYGRAPH_NO_CACHE_OPCODE,
         false, features()) {}

SemanticInstruction Python310Adapter::lift(const RawInstruction& instruction,
                                            const CodeMetadata& metadata) const {
    auto result = TableDrivenCPythonAdapter::lift(instruction, metadata);
    if (instruction.opcode == CPYGRAPH_PY310_ROT_TWO ||
        instruction.opcode == CPYGRAPH_PY310_ROT_THREE ||
        instruction.opcode == CPYGRAPH_PY310_ROT_FOUR)
        result.operand = instruction.opcode == CPYGRAPH_PY310_ROT_TWO
            ? 2U : instruction.opcode == CPYGRAPH_PY310_ROT_THREE ? 3U : 4U;
    if (instruction.opcode == CPYGRAPH_PY310_DUP_TOP) result.operand = 1;
    return result;
}

std::vector<ExceptionRegion> Python310Adapter::exceptionRegions(
    const std::vector<std::uint8_t>& bytes, const CodeMetadata& metadata) const {
    const auto raw = parser().parse(bytes);
    std::vector<ExceptionRegion> regions;
    std::vector<std::size_t> setup_indices;
    std::vector<bool> async_with_regions;
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const auto& instruction = raw[index];
        const bool setup_finally = instruction.opcode == CPYGRAPH_PY310_SETUP_FINALLY;
        const bool setup_with = instruction.opcode == CPYGRAPH_PY310_SETUP_WITH ||
                                instruction.opcode == CPYGRAPH_PY310_SETUP_ASYNC_WITH;
        if (!setup_finally && !setup_with) continue;
        if (index + 1 >= raw.size())
            throw std::invalid_argument("legacy exception setup has no protected instruction");
        constexpr std::uint32_t code_unit = 2;
        const auto jump_base = instruction.offset + instruction.prefix_size + code_unit;
        const auto delta = instruction.argument * code_unit;
        if (jump_base > std::numeric_limits<std::uint32_t>::max() - delta)
            throw std::overflow_error("legacy exception handler target exceeds bytecode offsets");
        const auto handler = jump_base + delta;
        // CPython 3.10 enters the handler with six exception-state values above
        // the stack depth preserved by SETUP_*.  For SETUP_WITH and
        // SETUP_ASYNC_WITH that preserved depth already includes the exit
        // callable.  WITH_EXCEPT_START temporarily pushes one more value, and
        // the generated POP_TOP/POP_EXCEPT cleanup consumes the resulting
        // seven values before control rejoins the normal path.
        regions.push_back({raw[index + 1].offset, handler, handler, 0,
                           6U, false});
        setup_indices.push_back(index);
        async_with_regions.push_back(
            instruction.opcode == CPYGRAPH_PY310_SETUP_ASYNC_WITH);
    }

    const auto program = CPythonAdapter::lift(bytes, metadata);
    std::unordered_map<std::uint32_t, std::size_t> offset_to_index;
    for (std::size_t index = 0; index < program.size(); ++index)
        offset_to_index.emplace(program[index].offset, index);
    std::vector<std::optional<std::uint32_t>> depths(program.size());
    std::deque<std::size_t> worklist;
    if (!program.empty()) {
        depths[0] = 0;
        worklist.push_back(0);
    }
    const auto enqueue = [&](std::size_t index, std::uint32_t depth) {
        if (index >= depths.size() || depths[index]) return;
        depths[index] = depth;
        worklist.push_back(index);
    };
    while (!worklist.empty()) {
        const auto index = worklist.front();
        worklist.pop_front();
        const auto depth = *depths[index];
        for (std::size_t region = 0; region < regions.size(); ++region) {
            if (setup_indices[region] == index) {
                // SETUP_WITH replaces its manager with __exit__ and __enter__,
                // so the protected depth equals the pre-op depth.  For
                // SETUP_ASYNC_WITH, BEFORE_ASYNC_WITH already produced the
                // retained exit callable and awaited enter result; the
                // following STORE removes that result before the body.
                regions[region].stack_depth = async_with_regions[region] && depth > 0
                    ? depth - 1U : depth;
            }
        }
        const auto& instruction = program[index];
        for (std::size_t region = 0; region < regions.size(); ++region) {
            if (instruction.offset < regions[region].start_offset ||
                instruction.offset >= regions[region].end_offset)
                continue;
            const auto handler = offset_to_index.find(regions[region].handler_offset);
            if (handler != offset_to_index.end())
                enqueue(handler->second, regions[region].stack_depth +
                                         regions[region].exception_stack_items);
        }
        if (instruction.opcode == SemanticOpcode::Return ||
            instruction.opcode == SemanticOpcode::Raise)
            continue;
        if (instruction.opcode == SemanticOpcode::Branch) {
            const auto target = offset_to_index.find(*instruction.jump_target);
            if (target != offset_to_index.end())
                if (const auto outgoing = stackAfter(depth, instruction, true))
                    enqueue(target->second, *outgoing);
            continue;
        }
        if (instruction.opcode == SemanticOpcode::ConditionalBranch) {
            const auto target = offset_to_index.find(*instruction.jump_target);
            if (target != offset_to_index.end())
                if (const auto outgoing = stackAfter(depth, instruction, true))
                    enqueue(target->second, *outgoing);
            if (index + 1U < program.size())
                if (const auto outgoing = stackAfter(depth, instruction, false))
                    enqueue(index + 1U, *outgoing);
            continue;
        }
        if (index + 1U < program.size())
            if (const auto outgoing = stackAfter(depth, instruction, false))
                enqueue(index + 1U, *outgoing);
    }
    return regions;
}

}  // namespace cpygraph::bytecode
