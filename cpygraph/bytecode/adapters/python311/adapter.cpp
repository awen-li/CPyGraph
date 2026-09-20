#include "bytecode/adapters/python311/adapter.h"
#include "bytecode/adapters/python311/opcodes.h"

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
    result.flagged_load_global = CPYGRAPH_PY311_LOAD_GLOBAL;
    result.receiver_opcodes = {CPYGRAPH_PY311_LOAD_METHOD};
    result.binary_op_opcode = CPYGRAPH_PY311_BINARY_OP;
    result.comparison_opcode = CPYGRAPH_PY311_COMPARE_OP;
    result.preserve_argument_opcodes = {CPYGRAPH_PY311_SWAP, CPYGRAPH_PY311_COPY};
    result.conditional_stack_rules = {
        {CPYGRAPH_PY311_FOR_ITER, 0, 1, 1, 0, 1},
        {CPYGRAPH_PY311_JUMP_IF_FALSE_OR_POP, 1, 0, 0, 0, 1},
        {CPYGRAPH_PY311_JUMP_IF_TRUE_OR_POP, 1, 0, 0, 0, 1},
        {CPYGRAPH_PY311_SEND, 1, 1, 1, 0, 0},
    };
    result.expanded_call_opcode = CPYGRAPH_PY311_CALL_FUNCTION_EX;
    result.normalize_create_function = true;
    result.create_function_flag_inputs = true;
    return result;
}
}  // namespace

Python311Adapter::Python311Adapter()
    : TableDrivenCPythonAdapter({
          {CPYGRAPH_PY311_POP_TOP, {O::Pop}},
          {CPYGRAPH_PY311_PUSH_NULL, {O::CallProtocolMarker}},          // PUSH_NULL
          {CPYGRAPH_PY311_NOP, {O::Nop}},
          {CPYGRAPH_PY311_UNARY_POSITIVE, unaryProtocol(P::Positive, M::Positive)},
          {CPYGRAPH_PY311_UNARY_NEGATIVE, unaryProtocol(P::Negative, M::Negative)},
          {CPYGRAPH_PY311_UNARY_NOT, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY311_UNARY_INVERT, unaryProtocol(P::Invert, M::Invert)},
          {CPYGRAPH_PY311_BINARY_SUBSCR, {O::LoadElement}},
          {CPYGRAPH_PY311_GET_LEN, peekProtocol(1U, 1U, P::DynamicProtocol, pythonMethod(M::Len))},
          {CPYGRAPH_PY311_MATCH_MAPPING, fixedStack(O::Generic, 0, 1, 1)},
          {CPYGRAPH_PY311_MATCH_SEQUENCE, fixedStack(O::Generic, 0, 1, 1)},
          {CPYGRAPH_PY311_MATCH_KEYS, fixedStack(O::Generic, 0, 1, 2)},
          {CPYGRAPH_PY311_PUSH_EXC_INFO, fixedStack(O::Generic, 1, 2)},    // PUSH_EXC_INFO
          {CPYGRAPH_PY311_CHECK_EXC_MATCH, fixedStack(O::Generic, 2, 2)},
          {CPYGRAPH_PY311_CHECK_EG_MATCH, fixedStack(O::Generic, 2, 2)},    // CHECK_EG_MATCH
          {CPYGRAPH_PY311_WITH_EXCEPT_START, fixedStack(O::Generic, 0, 1, 4)},
          {CPYGRAPH_PY311_GET_AITER, unaryProtocol(P::Iteration, M::AsyncIter)},
          {CPYGRAPH_PY311_GET_ANEXT, peekProtocol(1U, 1U, P::Iteration, pythonMethod(M::AsyncNext))},
          {CPYGRAPH_PY311_BEFORE_ASYNC_WITH, protocolStack(1U, 2U, P::DynamicProtocol, pythonMethod(M::AsyncEnter) | pythonMethod(M::AsyncExit))},
          {CPYGRAPH_PY311_BEFORE_WITH, fixedStack(O::EnterContext, 1, 2)},
          {CPYGRAPH_PY311_END_ASYNC_FOR, fixedStack(O::Generic, 2, 0)},
          {CPYGRAPH_PY311_STORE_SUBSCR, {O::StoreElement}},
          {CPYGRAPH_PY311_DELETE_SUBSCR, {O::DeleteElement}},
          {CPYGRAPH_PY311_GET_ITER, iterationProtocol()},
          {CPYGRAPH_PY311_GET_YIELD_FROM_ITER, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY311_PRINT_EXPR, fixedStack(O::Generic, 1, 0)},    // PRINT_EXPR
          {CPYGRAPH_PY311_LOAD_BUILD_CLASS, fixedStack(O::Generic, 0, 1)},
          {CPYGRAPH_PY311_LOAD_ASSERTION_ERROR, {O::LoadLiteral}},
          {CPYGRAPH_PY311_RETURN_GENERATOR, {O::LoadLiteral}},               // synthetic initial send value for following POP_TOP
          {CPYGRAPH_PY311_LIST_TO_TUPLE, fixedStack(O::Generic, 1, 1, 0, true)},
          {CPYGRAPH_PY311_RETURN_VALUE, {O::Return}},
          {CPYGRAPH_PY311_IMPORT_STAR, protocolStack(1U, 0U, P::Import, {})},
          {CPYGRAPH_PY311_SETUP_ANNOTATIONS, {O::Nop}},
          {CPYGRAPH_PY311_YIELD_VALUE, fixedStack(O::Yield, 1, 1)},
          {CPYGRAPH_PY311_ASYNC_GEN_WRAP, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY311_PREP_RERAISE_STAR, fixedStack(O::Generic, 2, 1)},    // PREP_RERAISE_STAR
          {CPYGRAPH_PY311_POP_EXCEPT, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY311_STORE_NAME, {O::StoreGlobal, S::Name}},
          {CPYGRAPH_PY311_DELETE_NAME, {O::DeleteGlobal, S::Name}},
          {CPYGRAPH_PY311_UNPACK_SEQUENCE, collectionUnpack(StackEffect::Unpack)},
          {CPYGRAPH_PY311_FOR_ITER, iterationNextProtocol()},
          {CPYGRAPH_PY311_UNPACK_EX, collectionUnpack(StackEffect::UnpackEx)},
          {CPYGRAPH_PY311_STORE_ATTR, {O::StoreAttribute, S::Name}},
          {CPYGRAPH_PY311_DELETE_ATTR, deleteAttributeProtocol()},
          {CPYGRAPH_PY311_STORE_GLOBAL, {O::StoreGlobal, S::Name}},
          {CPYGRAPH_PY311_DELETE_GLOBAL, {O::DeleteGlobal, S::Name}},
          {CPYGRAPH_PY311_SWAP, {O::StackSwap}},
          {CPYGRAPH_PY311_LOAD_CONST, {O::LoadConst, S::Constant}},
          {CPYGRAPH_PY311_LOAD_NAME, {O::LoadGlobal, S::Name}},
          {CPYGRAPH_PY311_BUILD_TUPLE, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY311_BUILD_LIST, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY311_BUILD_SET, collectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY311_BUILD_MAP, collectionResult(StackEffect::MapToOne)},
          {CPYGRAPH_PY311_LOAD_ATTR, {O::LoadAttribute, S::Name}},
          {CPYGRAPH_PY311_COMPARE_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY311_IMPORT_NAME, {O::ImportModule, S::Name}},
          {CPYGRAPH_PY311_IMPORT_FROM, {O::ImportAttribute, S::Name}},
          {CPYGRAPH_PY311_JUMP_FORWARD, {O::Branch, S::None, J::RelativeForward}},
          {CPYGRAPH_PY311_JUMP_IF_FALSE_OR_POP, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY311_JUMP_IF_TRUE_OR_POP, {O::ConditionalBranch, S::None, J::RelativeForward, true}},
          {CPYGRAPH_PY311_POP_JUMP_FORWARD_IF_FALSE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY311_POP_JUMP_FORWARD_IF_TRUE, {O::ConditionalBranch, S::None, J::RelativeForward, true}},
          {CPYGRAPH_PY311_LOAD_GLOBAL, {O::LoadGlobal, S::Name}},
          {CPYGRAPH_PY311_IS_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY311_CONTAINS_OP, protocolStack(2U, 1U, P::DynamicProtocol, pythonMethod(M::Contains))},
          {CPYGRAPH_PY311_RERAISE, fixedStack(O::Raise, 1, 0)},
          {CPYGRAPH_PY311_COPY, {O::StackCopy}},
          {CPYGRAPH_PY311_BINARY_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY311_SEND, {O::ConditionalBranch, S::None, J::RelativeForward, false}}, // SEND
          {CPYGRAPH_PY311_LOAD_FAST, {O::LoadLocal, S::Local}},
          {CPYGRAPH_PY311_STORE_FAST, {O::StoreLocal, S::Local}},
          {CPYGRAPH_PY311_DELETE_FAST, {O::DeleteLocal, S::Local}},
          {CPYGRAPH_PY311_POP_JUMP_FORWARD_IF_NOT_NONE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY311_POP_JUMP_FORWARD_IF_NONE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY311_RAISE_VARARGS, dynamicStack(O::Raise, StackEffect::RaiseArguments)},
          {CPYGRAPH_PY311_GET_AWAITABLE, unaryProtocol(P::DynamicProtocol, M::Await)},
          {CPYGRAPH_PY311_MAKE_FUNCTION, {O::CreateFunction, S::Count}},
          {CPYGRAPH_PY311_BUILD_SLICE, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY311_JUMP_BACKWARD_NO_INTERRUPT, {O::Branch, S::None, J::RelativeBackward}},
          {CPYGRAPH_PY311_MAKE_CELL,
           lexicalAccess(O::Nop, LexicalAccessKind::CellCreation)},
          {CPYGRAPH_PY311_LOAD_CLOSURE,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellReference)},
          {CPYGRAPH_PY311_LOAD_DEREF,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY311_STORE_DEREF,
           lexicalAccess(O::StoreLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY311_DELETE_DEREF,
           lexicalAccess(O::DeleteLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY311_JUMP_BACKWARD, {O::Branch, S::None, J::RelativeBackward}},
          {CPYGRAPH_PY311_CALL_FUNCTION_EX, {O::Call, S::Count}},
          {CPYGRAPH_PY311_LIST_APPEND, collectionStore(1)},             // LIST_APPEND
          {CPYGRAPH_PY311_SET_ADD, collectionStore(1)},             // SET_ADD
          {CPYGRAPH_PY311_MAP_ADD, collectionStore(2)},             // MAP_ADD
          {CPYGRAPH_PY311_LOAD_CLASSDEREF,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY311_COPY_FREE_VARS, {O::Nop}},
          {CPYGRAPH_PY311_RESUME, {O::Nop}},                       // RESUME
          {CPYGRAPH_PY311_MATCH_CLASS, fixedStack(O::Generic, 3, 1)},
          {CPYGRAPH_PY311_FORMAT_VALUE, dynamicProtocol(StackEffect::FormatValue, P::DynamicProtocol, pythonMethod(M::Format))},
          {CPYGRAPH_PY311_BUILD_CONST_KEY_MAP, collectionResult(StackEffect::ConstKeyMapToOne)},
          {CPYGRAPH_PY311_BUILD_STRING, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY311_LOAD_METHOD, {O::LoadAttribute, S::Name}},    // LOAD_METHOD
          {CPYGRAPH_PY311_LIST_EXTEND, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY311_SET_UPDATE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY311_DICT_MERGE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY311_DICT_UPDATE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY311_PRECALL, {O::Nop}},                       // PRECALL
          {CPYGRAPH_PY311_CALL, {O::Call, S::Count}},
          {CPYGRAPH_PY311_KW_NAMES, {O::PrepareKeywordCall, S::Constant}}, // KW_NAMES
          {CPYGRAPH_PY311_POP_JUMP_BACKWARD_IF_NOT_NONE, {O::ConditionalBranch, S::None, J::RelativeBackward, false}},
          {CPYGRAPH_PY311_POP_JUMP_BACKWARD_IF_NONE, {O::ConditionalBranch, S::None, J::RelativeBackward, false}},
          {CPYGRAPH_PY311_POP_JUMP_BACKWARD_IF_FALSE, {O::ConditionalBranch, S::None, J::RelativeBackward, false}},
          {CPYGRAPH_PY311_POP_JUMP_BACKWARD_IF_TRUE, {O::ConditionalBranch, S::None, J::RelativeBackward, true}},
      }, CPYGRAPH_PY311_EXTENDED_ARG, true, CPYGRAPH_PY311_CACHE,
         true, features()) {}

}  // namespace cpygraph::bytecode
