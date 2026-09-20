#include "bytecode/adapters/python312/adapter.h"
#include "bytecode/adapters/python312/opcodes.h"

namespace cpygraph::bytecode {
namespace {
using O = SemanticOpcode;
using S = OperandSource;
using J = JumpKind;
using P = PythonProtocolOperation;
using M = PythonSpecialMethod;
AdapterFeatures features() {
    AdapterFeatures result;
    result.call_protocol_inputs = 2;
    result.flagged_load_global = CPYGRAPH_PY312_LOAD_GLOBAL;
    result.flagged_load_attribute = CPYGRAPH_PY312_LOAD_ATTR;
    result.method_super_attribute = CPYGRAPH_PY312_LOAD_SUPER_ATTR;
    result.method_super_attribute_name_shift =
        CPYGRAPH_PY312_SUPER_ATTRIBUTE_NAME_SHIFT;
    result.binary_op_opcode = CPYGRAPH_PY312_BINARY_OP;
    result.comparison_opcode = CPYGRAPH_PY312_COMPARE_OP;
    result.comparison_argument_shift =
        CPYGRAPH_PY312_COMPARE_ARGUMENT_SHIFT;
    result.import_star_intrinsic_opcode = CPYGRAPH_PY312_CALL_INTRINSIC_1;
    result.import_star_intrinsic_operand =
        CPYGRAPH_PY312_INTRINSIC_IMPORT_STAR;
    result.clear_local_opcodes = {CPYGRAPH_PY312_LOAD_FAST_AND_CLEAR};
    result.preserve_argument_opcodes = {CPYGRAPH_PY312_SWAP, CPYGRAPH_PY312_COPY};
    result.jump_cache_adjusted_opcodes = {CPYGRAPH_PY312_FOR_ITER, CPYGRAPH_PY312_SEND};
    result.conditional_stack_rules = {
        {CPYGRAPH_PY312_FOR_ITER, 0, 1, 0, 1, 1},
        {CPYGRAPH_PY312_SEND, 1, 1, 1, 1, 0},
    };
    result.expanded_call_opcode = CPYGRAPH_PY312_CALL_FUNCTION_EX;
    result.normalize_create_function = true;
    result.create_function_flag_inputs = true;
    return result;
}
}

Python312Adapter::Python312Adapter()
    : TableDrivenCPythonAdapter({
          {CPYGRAPH_PY312_POP_TOP, {O::Pop}},
          {CPYGRAPH_PY312_PUSH_NULL, {O::CallProtocolMarker}},          // PUSH_NULL
          {CPYGRAPH_PY312_END_FOR, fixedStack(O::Generic, 2, 0)},
          {CPYGRAPH_PY312_END_SEND, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY312_NOP, {O::Nop}},
          {CPYGRAPH_PY312_UNARY_NEGATIVE, unaryProtocol(P::Negative, M::Negative)},
          {CPYGRAPH_PY312_UNARY_NOT, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY312_UNARY_INVERT, unaryProtocol(P::Invert, M::Invert)},
          {CPYGRAPH_PY312_BINARY_SUBSCR, {O::LoadElement}},
          {CPYGRAPH_PY312_BINARY_SLICE, fixedStack(O::Generic, 3, 1)},
          {CPYGRAPH_PY312_STORE_SLICE, fixedStack(O::Generic, 4, 0)},
          {CPYGRAPH_PY312_GET_LEN, peekProtocol(1U, 1U, P::DynamicProtocol, pythonMethod(M::Len))},
          {CPYGRAPH_PY312_MATCH_MAPPING, fixedStack(O::Generic, 0, 1, 1)},
          {CPYGRAPH_PY312_MATCH_SEQUENCE, fixedStack(O::Generic, 0, 1, 1)},
          {CPYGRAPH_PY312_MATCH_KEYS, fixedStack(O::Generic, 0, 1, 2)},
          {CPYGRAPH_PY312_PUSH_EXC_INFO, fixedStack(O::Generic, 1, 2)},
          {CPYGRAPH_PY312_CHECK_EXC_MATCH, fixedStack(O::Generic, 2, 2)},
          {CPYGRAPH_PY312_CHECK_EG_MATCH, fixedStack(O::Generic, 2, 2)},    // CHECK_EG_MATCH
          {CPYGRAPH_PY312_WITH_EXCEPT_START, fixedStack(O::Generic, 0, 1, 4)},
          {CPYGRAPH_PY312_GET_AITER, unaryProtocol(P::Iteration, M::AsyncIter)},
          {CPYGRAPH_PY312_GET_ANEXT, peekProtocol(1U, 1U, P::Iteration, pythonMethod(M::AsyncNext))},
          {CPYGRAPH_PY312_BEFORE_ASYNC_WITH, protocolStack(1U, 2U, P::DynamicProtocol, pythonMethod(M::AsyncEnter) | pythonMethod(M::AsyncExit))},
          {CPYGRAPH_PY312_BEFORE_WITH, fixedStack(O::EnterContext, 1, 2)},
          {CPYGRAPH_PY312_END_ASYNC_FOR, fixedStack(O::Generic, 2, 0)},
          {CPYGRAPH_PY312_CLEANUP_THROW, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY312_STORE_SUBSCR, {O::StoreElement}},
          {CPYGRAPH_PY312_DELETE_SUBSCR, {O::DeleteElement}},
          {CPYGRAPH_PY312_GET_ITER, iterationProtocol()},
          {CPYGRAPH_PY312_GET_YIELD_FROM_ITER, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY312_LOAD_BUILD_CLASS, fixedStack(O::Generic, 0, 1)},
          {CPYGRAPH_PY312_LOAD_ASSERTION_ERROR, {O::LoadLiteral}},
          {CPYGRAPH_PY312_RETURN_GENERATOR, {O::LoadLiteral}},               // synthetic initial send value for following POP_TOP
          {CPYGRAPH_PY312_RETURN_VALUE, {O::Return}},
          {CPYGRAPH_PY312_SETUP_ANNOTATIONS, {O::Nop}},
          {CPYGRAPH_PY312_LOAD_LOCALS, fixedStack(O::Generic, 0, 1)},
          {CPYGRAPH_PY312_POP_EXCEPT, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY312_STORE_NAME, {O::StoreGlobal, S::Name}},
          {CPYGRAPH_PY312_DELETE_NAME, {O::DeleteGlobal, S::Name}},
          {CPYGRAPH_PY312_UNPACK_SEQUENCE, collectionUnpack(StackEffect::Unpack)},
          {CPYGRAPH_PY312_FOR_ITER, iterationNextProtocol()},
          {CPYGRAPH_PY312_UNPACK_EX, collectionUnpack(StackEffect::UnpackEx)},
          {CPYGRAPH_PY312_STORE_ATTR, {O::StoreAttribute, S::Name}},
          {CPYGRAPH_PY312_DELETE_ATTR, deleteAttributeProtocol()},
          {CPYGRAPH_PY312_STORE_GLOBAL, {O::StoreGlobal, S::Name}},
          {CPYGRAPH_PY312_DELETE_GLOBAL, {O::DeleteGlobal, S::Name}},
          {CPYGRAPH_PY312_SWAP, {O::StackSwap}},
          {CPYGRAPH_PY312_LOAD_CONST, {O::LoadConst, S::Constant}},
          {CPYGRAPH_PY312_LOAD_NAME, {O::LoadGlobal, S::Name}},
          {CPYGRAPH_PY312_BUILD_TUPLE, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY312_BUILD_LIST, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY312_BUILD_SET, collectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY312_BUILD_MAP, collectionResult(StackEffect::MapToOne)},
          {CPYGRAPH_PY312_LOAD_ATTR, {O::LoadAttribute, S::Name}},
          {CPYGRAPH_PY312_COMPARE_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY312_IMPORT_NAME, {O::ImportModule, S::Name}},
          {CPYGRAPH_PY312_IMPORT_FROM, {O::ImportAttribute, S::Name}},
          {CPYGRAPH_PY312_JUMP_FORWARD, {O::Branch, S::None, J::RelativeForward}},
          {CPYGRAPH_PY312_POP_JUMP_IF_FALSE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY312_POP_JUMP_IF_TRUE, {O::ConditionalBranch, S::None, J::RelativeForward, true}},
          {CPYGRAPH_PY312_LOAD_GLOBAL, {O::LoadGlobal, S::Name}},
          {CPYGRAPH_PY312_IS_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY312_CONTAINS_OP, protocolStack(2U, 1U, P::DynamicProtocol, pythonMethod(M::Contains))},
          {CPYGRAPH_PY312_RERAISE, fixedStack(O::Raise, 1, 0)},
          {CPYGRAPH_PY312_COPY, {O::StackCopy}},
          {CPYGRAPH_PY312_RETURN_CONST, {O::Return, S::Constant, J::None, false, true}}, // RETURN_CONST
          {CPYGRAPH_PY312_BINARY_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY312_SEND, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY312_LOAD_FAST, {O::LoadLocal, S::Local}},
          {CPYGRAPH_PY312_STORE_FAST, {O::StoreLocal, S::Local}},
          {CPYGRAPH_PY312_DELETE_FAST, {O::DeleteLocal, S::Local}},
          {CPYGRAPH_PY312_LOAD_FAST_CHECK, {O::LoadLocal, S::Local}},
          {CPYGRAPH_PY312_POP_JUMP_IF_NOT_NONE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY312_POP_JUMP_IF_NONE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY312_RAISE_VARARGS, dynamicStack(O::Raise, StackEffect::RaiseArguments)},
          {CPYGRAPH_PY312_GET_AWAITABLE, unaryProtocol(P::DynamicProtocol, M::Await)},
          {CPYGRAPH_PY312_MAKE_FUNCTION, {O::CreateFunction, S::Count}},
          {CPYGRAPH_PY312_BUILD_SLICE, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY312_JUMP_BACKWARD_NO_INTERRUPT, {O::Branch, S::None, J::RelativeBackward}},
          {CPYGRAPH_PY312_MAKE_CELL,
           lexicalAccess(O::Nop, LexicalAccessKind::CellCreation)},
          {CPYGRAPH_PY312_LOAD_CLOSURE,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellReference)},
          {CPYGRAPH_PY312_LOAD_DEREF,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY312_STORE_DEREF,
           lexicalAccess(O::StoreLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY312_DELETE_DEREF,
           lexicalAccess(O::DeleteLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY312_JUMP_BACKWARD, {O::Branch, S::None, J::RelativeBackward}},
          {CPYGRAPH_PY312_LOAD_SUPER_ATTR, {O::LoadAttribute, S::Name}},
          {CPYGRAPH_PY312_CALL_FUNCTION_EX, {O::Call, S::Count}},
          {CPYGRAPH_PY312_LOAD_FAST_AND_CLEAR, {O::LoadLocal, S::Local}},
          {CPYGRAPH_PY312_LIST_APPEND, collectionStore(1)},             // LIST_APPEND
          {CPYGRAPH_PY312_SET_ADD, collectionStore(1)},             // SET_ADD
          {CPYGRAPH_PY312_MAP_ADD, collectionStore(2)},             // MAP_ADD
          {CPYGRAPH_PY312_COPY_FREE_VARS, {O::Nop}},
          {CPYGRAPH_PY312_YIELD_VALUE, fixedStack(O::Yield, 1, 1)},
          {CPYGRAPH_PY312_RESUME, {O::Nop}},                       // RESUME
          {CPYGRAPH_PY312_MATCH_CLASS, fixedStack(O::Generic, 3, 1)},
          {CPYGRAPH_PY312_FORMAT_VALUE, dynamicProtocol(StackEffect::FormatValue, P::DynamicProtocol, pythonMethod(M::Format))},
          {CPYGRAPH_PY312_BUILD_CONST_KEY_MAP, collectionResult(StackEffect::ConstKeyMapToOne)},
          {CPYGRAPH_PY312_BUILD_STRING, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY312_LIST_EXTEND, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY312_SET_UPDATE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY312_DICT_MERGE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY312_DICT_UPDATE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY312_PRECALL_COMPAT, {O::Nop}},                       // PRECALL compatibility form
          {CPYGRAPH_PY312_CALL, {O::Call, S::Count}},
          {CPYGRAPH_PY312_KW_NAMES, {O::PrepareKeywordCall, S::Constant}}, // KW_NAMES
          {CPYGRAPH_PY312_CALL_INTRINSIC_1, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY312_CALL_INTRINSIC_2, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY312_LOAD_FROM_DICT_OR_GLOBALS, fixedStack(O::Generic, 1, 1)},   // LOAD_FROM_DICT_OR_GLOBALS
          {CPYGRAPH_PY312_LOAD_FROM_DICT_OR_DEREF, fixedStack(O::Generic, 1, 1)},
      }, CPYGRAPH_PY312_EXTENDED_ARG, true, CPYGRAPH_PY312_CACHE,
         true, features()) {}

}  // namespace cpygraph::bytecode
