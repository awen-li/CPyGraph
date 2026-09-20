#include "bytecode/adapters/python314/adapter.h"
#include "bytecode/adapters/binary_operations.h"
#include "bytecode/adapters/python314/opcodes.h"

#include <stdexcept>

namespace cpygraph::bytecode {
namespace {
using O = SemanticOpcode;
using S = OperandSource;
using J = JumpKind;
using P = PythonProtocolOperation;
using M = PythonSpecialMethod;
AdapterFeatures features() {
    AdapterFeatures result;
    result.binary_op_opcode = CPYGRAPH_PY314_BINARY_OP;
    result.call_protocol_inputs = 2;
    result.flagged_load_global = CPYGRAPH_PY314_LOAD_GLOBAL;
    result.flagged_load_attribute = CPYGRAPH_PY314_LOAD_ATTR;
    result.method_super_attribute = CPYGRAPH_PY314_LOAD_SUPER_ATTR;
    result.method_super_attribute_name_shift =
        CPYGRAPH_PY314_SUPER_ATTRIBUTE_NAME_SHIFT;
    result.comparison_opcode = CPYGRAPH_PY314_COMPARE_OP;
    result.comparison_argument_shift =
        CPYGRAPH_PY314_COMPARE_ARGUMENT_SHIFT;
    result.import_star_intrinsic_opcode = CPYGRAPH_PY314_CALL_INTRINSIC_1;
    result.import_star_intrinsic_operand =
        CPYGRAPH_PY314_INTRINSIC_IMPORT_STAR;
    result.clear_local_opcodes = {CPYGRAPH_PY314_LOAD_FAST_AND_CLEAR};
    result.preserve_argument_opcodes = {CPYGRAPH_PY314_COPY, CPYGRAPH_PY314_SWAP};
    result.jump_cache_adjusted_opcodes = {
        CPYGRAPH_PY314_FOR_ITER, CPYGRAPH_PY314_JUMP_BACKWARD,
        CPYGRAPH_PY314_POP_JUMP_IF_FALSE, CPYGRAPH_PY314_POP_JUMP_IF_TRUE,
        CPYGRAPH_PY314_POP_JUMP_IF_NONE, CPYGRAPH_PY314_POP_JUMP_IF_NOT_NONE,
        CPYGRAPH_PY314_SEND};
    result.combined_local_opcodes = {
        CPYGRAPH_PY314_LOAD_FAST_LOAD_FAST, CPYGRAPH_PY314_LOAD_FAST_BORROW_LOAD_FAST_BORROW,
        CPYGRAPH_PY314_STORE_FAST_LOAD_FAST, CPYGRAPH_PY314_STORE_FAST_STORE_FAST};
    result.conditional_stack_rules = {
        {CPYGRAPH_PY314_FOR_ITER, 0, 1, 0, 1, 1},
        {CPYGRAPH_PY314_SEND, 1, 1, 1, 1, 0},
    };
    result.expanded_call_opcode = CPYGRAPH_PY314_CALL_FUNCTION_EX;
    result.expanded_call_operand = ExpandedCallOperand::ArgsAndKeywords;
    result.keyword_call_opcode = CPYGRAPH_PY314_CALL_KW;
    result.normalize_create_function = true;
    result.cell_reference_local_opcode = CPYGRAPH_PY314_LOAD_FAST;
    return result;
}
}  // namespace

Python314Adapter::Python314Adapter()
    : TableDrivenCPythonAdapter({
          {CPYGRAPH_PY314_BINARY_SLICE, fixedStack(O::Generic, 3, 1)},
          {CPYGRAPH_PY314_BUILD_TEMPLATE, fixedStack(O::Generic, 2, 1)},     // BUILD_TEMPLATE
          {CPYGRAPH_PY314_CALL_FUNCTION_EX, {O::Call, S::Count}},
          {CPYGRAPH_PY314_CHECK_EG_MATCH, fixedStack(O::Generic, 2, 2)},     // CHECK_EG_MATCH
          {CPYGRAPH_PY314_CHECK_EXC_MATCH, fixedStack(O::Generic, 2, 2)},
          {CPYGRAPH_PY314_CLEANUP_THROW, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY314_DELETE_SUBSCR, {O::DeleteElement}},
          {CPYGRAPH_PY314_END_FOR, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY314_END_SEND, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY314_FORMAT_SIMPLE, unaryProtocol(P::DynamicProtocol, M::Format)},
          {CPYGRAPH_PY314_FORMAT_WITH_SPEC, protocolStack(2U, 1U, P::DynamicProtocol, pythonMethod(M::Format))},
          {CPYGRAPH_PY314_GET_AITER, unaryProtocol(P::Iteration, M::AsyncIter)},
          {CPYGRAPH_PY314_GET_ANEXT, peekProtocol(1U, 1U, P::Iteration, pythonMethod(M::AsyncNext))},
          {CPYGRAPH_PY314_GET_ITER, iterationProtocol()},
          {CPYGRAPH_PY314_GET_LEN, peekProtocol(1U, 1U, P::DynamicProtocol, pythonMethod(M::Len))},
          {CPYGRAPH_PY314_GET_YIELD_FROM_ITER, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY314_LOAD_BUILD_CLASS, fixedStack(O::Generic, 0, 1)},
          {CPYGRAPH_PY314_LOAD_LOCALS, fixedStack(O::Generic, 0, 1)},
          {CPYGRAPH_PY314_MAKE_FUNCTION, {O::CreateFunction}},
          {CPYGRAPH_PY314_MATCH_KEYS, fixedStack(O::Generic, 0, 1, 2)}, // MATCH_KEYS
          {CPYGRAPH_PY314_MATCH_MAPPING, fixedStack(O::Generic, 0, 1, 1)}, // MATCH_MAPPING
          {CPYGRAPH_PY314_MATCH_SEQUENCE, fixedStack(O::Generic, 0, 1, 1)},
          {CPYGRAPH_PY314_NOP, {O::Nop}},
          {CPYGRAPH_PY314_NOT_TAKEN, {O::Nop}},
          {CPYGRAPH_PY314_POP_EXCEPT, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY314_POP_ITER, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY314_POP_TOP, {O::Pop}},
          {CPYGRAPH_PY314_PUSH_EXC_INFO, fixedStack(O::Generic, 1, 2)},
          {CPYGRAPH_PY314_PUSH_NULL, {O::CallProtocolMarker}},         // PUSH_NULL
          {CPYGRAPH_PY314_RETURN_GENERATOR, {O::LoadLiteral}},               // RETURN_GENERATOR resume input
          {CPYGRAPH_PY314_RETURN_VALUE, {O::Return}},
          {CPYGRAPH_PY314_SETUP_ANNOTATIONS, {O::Nop}},
          {CPYGRAPH_PY314_STORE_SLICE, fixedStack(O::Generic, 4, 0)},
          {CPYGRAPH_PY314_STORE_SUBSCR, {O::StoreElement}},
          {CPYGRAPH_PY314_TO_BOOL, preservingStack(1, 1)},
          {CPYGRAPH_PY314_UNARY_INVERT, unaryProtocol(P::Invert, M::Invert)},
          {CPYGRAPH_PY314_UNARY_NEGATIVE, unaryProtocol(P::Negative, M::Negative)},
          {CPYGRAPH_PY314_UNARY_NOT, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY314_WITH_EXCEPT_START, fixedStack(O::Generic, 0, 1, 4)},
          {CPYGRAPH_PY314_BINARY_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY314_BUILD_INTERPOLATION, fixedStack(O::Generic, 2, 1)},    // BUILD_INTERPOLATION
          {CPYGRAPH_PY314_BUILD_LIST, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY314_BUILD_MAP, collectionResult(StackEffect::MapToOne)},
          {CPYGRAPH_PY314_BUILD_SET, collectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY314_BUILD_SLICE, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY314_BUILD_STRING, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY314_BUILD_TUPLE, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY314_CALL, {O::Call, S::Count}},
          {CPYGRAPH_PY314_CALL_INTRINSIC_1, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY314_CALL_INTRINSIC_2, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY314_CALL_KW, {O::Call, S::Count}},             // CALL_KW
          {CPYGRAPH_PY314_COMPARE_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY314_CONTAINS_OP, protocolStack(2U, 1U, P::DynamicProtocol, pythonMethod(M::Contains))},
          {CPYGRAPH_PY314_CONVERT_VALUE, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY314_COPY, {O::StackCopy}},
          {CPYGRAPH_PY314_COPY_FREE_VARS, {O::Nop}},
          {CPYGRAPH_PY314_DELETE_ATTR, deleteAttributeProtocol()},
          {CPYGRAPH_PY314_DELETE_DEREF,
           lexicalAccess(O::DeleteLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY314_DELETE_FAST, {O::DeleteLocal, S::Deref}},
          {CPYGRAPH_PY314_DELETE_GLOBAL, {O::DeleteGlobal, S::Name}},
          {CPYGRAPH_PY314_DELETE_NAME, {O::DeleteGlobal, S::Name}},
          {CPYGRAPH_PY314_DICT_MERGE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY314_DICT_UPDATE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY314_END_ASYNC_FOR, fixedStack(O::Generic, 2, 0)},
          {CPYGRAPH_PY314_EXTENDED_ARG, {O::Nop}},                        // EXTENDED_ARG handled by parser
          {CPYGRAPH_PY314_FOR_ITER, iterationNextProtocol()},
          {CPYGRAPH_PY314_GET_AWAITABLE, unaryProtocol(P::DynamicProtocol, M::Await)},
          {CPYGRAPH_PY314_IMPORT_FROM, {O::ImportAttribute, S::Name}},
          {CPYGRAPH_PY314_IMPORT_NAME, {O::ImportModule, S::Name}},
          {CPYGRAPH_PY314_IS_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY314_JUMP_BACKWARD, {O::Branch, S::None, J::RelativeBackward}},
          {CPYGRAPH_PY314_JUMP_BACKWARD_NO_INTERRUPT, {O::Branch, S::None, J::RelativeBackward}},
          {CPYGRAPH_PY314_JUMP_FORWARD, {O::Branch, S::None, J::RelativeForward}},
          {CPYGRAPH_PY314_LIST_APPEND, collectionStore(1)},              // LIST_APPEND
          {CPYGRAPH_PY314_LIST_EXTEND, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY314_LOAD_ATTR, {O::LoadAttribute, S::Name}},
          {CPYGRAPH_PY314_LOAD_COMMON_CONSTANT, {O::LoadLiteral}},
          {CPYGRAPH_PY314_LOAD_CONST, {O::LoadConst, S::Constant}},
          {CPYGRAPH_PY314_LOAD_DEREF,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY314_LOAD_FAST, {O::LoadLocal, S::Deref}},
          {CPYGRAPH_PY314_LOAD_FAST_AND_CLEAR, {O::LoadLocal, S::Deref}},
          {CPYGRAPH_PY314_LOAD_FAST_BORROW, {O::LoadLocal, S::Deref}},        // LOAD_FAST_BORROW
          {CPYGRAPH_PY314_LOAD_FAST_BORROW_LOAD_FAST_BORROW, {O::LoadLocalPair}},
          {CPYGRAPH_PY314_LOAD_FAST_CHECK, {O::LoadLocal, S::Deref}},        // LOAD_FAST_CHECK
          {CPYGRAPH_PY314_LOAD_FAST_LOAD_FAST, {O::LoadLocalPair}},
          {CPYGRAPH_PY314_LOAD_FROM_DICT_OR_DEREF, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY314_LOAD_FROM_DICT_OR_GLOBALS, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY314_LOAD_GLOBAL, {O::LoadGlobal, S::Name}},
          {CPYGRAPH_PY314_LOAD_NAME, {O::LoadGlobal, S::Name}},        // LOAD_NAME
          {CPYGRAPH_PY314_LOAD_SMALL_INT, {O::LoadLiteral}},
          {CPYGRAPH_PY314_LOAD_SPECIAL, {O::LoadAttribute}},
          {CPYGRAPH_PY314_LOAD_SUPER_ATTR, {O::LoadAttribute, S::Name}},
          {CPYGRAPH_PY314_MAKE_CELL,
           lexicalAccess(O::Nop, LexicalAccessKind::CellCreation)},
          {CPYGRAPH_PY314_MAP_ADD, collectionStore(2)},              // MAP_ADD
          {CPYGRAPH_PY314_MATCH_CLASS, fixedStack(O::Generic, 3, 1)},
          {CPYGRAPH_PY314_POP_JUMP_IF_FALSE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY314_POP_JUMP_IF_NONE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY314_POP_JUMP_IF_NOT_NONE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY314_POP_JUMP_IF_TRUE, {O::ConditionalBranch, S::None, J::RelativeForward, true}},
          {CPYGRAPH_PY314_RAISE_VARARGS, dynamicStack(O::Raise, StackEffect::RaiseArguments)},
          {CPYGRAPH_PY314_RERAISE, fixedStack(O::Raise, 1, 0)},
          {CPYGRAPH_PY314_SEND, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY314_SET_ADD, collectionStore(1)},             // SET_ADD
          {CPYGRAPH_PY314_SET_FUNCTION_ATTRIBUTE, {O::SetFunctionAttribute, S::Count}},
          {CPYGRAPH_PY314_SET_UPDATE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY314_STORE_ATTR, {O::StoreAttribute, S::Name}},
          {CPYGRAPH_PY314_STORE_DEREF,
           lexicalAccess(O::StoreLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY314_STORE_FAST, {O::StoreLocal, S::Deref}},
          {CPYGRAPH_PY314_STORE_FAST_LOAD_FAST, {O::StoreLoadLocal}},
          {CPYGRAPH_PY314_STORE_FAST_STORE_FAST, {O::StoreLocalPair}},
          {CPYGRAPH_PY314_STORE_GLOBAL, {O::StoreGlobal, S::Name}},
          {CPYGRAPH_PY314_STORE_NAME, {O::StoreGlobal, S::Name}},      // STORE_NAME
          {CPYGRAPH_PY314_SWAP, {O::StackSwap}},
          {CPYGRAPH_PY314_UNPACK_EX, collectionUnpack(StackEffect::UnpackEx)},
          {CPYGRAPH_PY314_UNPACK_SEQUENCE, collectionUnpack(StackEffect::Unpack)},
          {CPYGRAPH_PY314_YIELD_VALUE, fixedStack(O::Yield, 1, 1)},
          {CPYGRAPH_PY314_RESUME, {O::Nop}},                       // RESUME
      }, CPYGRAPH_PY314_EXTENDED_ARG, true, CPYGRAPH_PY314_CACHE,
         true, features()) {}

SemanticInstruction Python314Adapter::lift(const RawInstruction& instruction,
                                            const CodeMetadata& metadata) const {
    auto result = TableDrivenCPythonAdapter::lift(instruction, metadata);
    // CPython 3.14 folds subscription into BINARY_OP using NB_SUBSCR (26).
    // Normalize it here so every core analysis continues to consume the
    // version-independent element-load operation.
    if (instruction.opcode == CPYGRAPH_PY314_BINARY_OP &&
        instruction.argument == CPYGRAPH_BINARY_OP_SUBSCRIPT) {
        result.opcode = SemanticOpcode::LoadElement;
        result.stack_input_count = 2;
        result.stack_output_count = 1;
        result.stack_peek_count = 0;
    }
    if (instruction.opcode == CPYGRAPH_PY314_BUILD_INTERPOLATION &&
        (instruction.argument & 1U) != 0U)
        result.stack_input_count = 3;  // explicit format specification
    if (instruction.opcode == CPYGRAPH_PY314_LOAD_SMALL_INT)
        result.constant_integer = instruction.argument;
    if (instruction.opcode == CPYGRAPH_PY314_LOAD_SPECIAL) {
        result.push_call_receiver = true;
        switch (instruction.argument) {
            case CPYGRAPH_PY314_SPECIAL_ENTER:
                result.symbol = specialMethodName(M::Enter);
                result.protocol_operation = P::ContextEnter;
                result.protocol_methods = pythonMethod(M::Enter);
                break;
            case CPYGRAPH_PY314_SPECIAL_EXIT:
                result.symbol = specialMethodName(M::Exit);
                result.protocol_operation = P::ContextExit;
                result.protocol_methods = pythonMethod(M::Exit);
                break;
            case CPYGRAPH_PY314_SPECIAL_ASYNC_ENTER:
                result.symbol = specialMethodName(M::AsyncEnter);
                result.protocol_operation = P::DynamicProtocol;
                result.protocol_methods = pythonMethod(M::AsyncEnter);
                break;
            case CPYGRAPH_PY314_SPECIAL_ASYNC_EXIT:
                result.symbol = specialMethodName(M::AsyncExit);
                result.protocol_operation = P::DynamicProtocol;
                result.protocol_methods = pythonMethod(M::AsyncExit);
                break;
            default:
                throw std::runtime_error("unknown CPython 3.14 LOAD_SPECIAL operand");
        }
    }
    return result;
}

}  // namespace cpygraph::bytecode
