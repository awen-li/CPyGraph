#include "bytecode/adapters/python313/adapter.h"
#include "bytecode/adapters/python313/opcodes.h"

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
    result.binary_op_opcode = CPYGRAPH_PY313_BINARY_OP;
    result.call_protocol_inputs = 2;
    result.flagged_load_global = CPYGRAPH_PY313_LOAD_GLOBAL;
    result.flagged_load_attribute = CPYGRAPH_PY313_LOAD_ATTR;
    result.method_super_attribute = CPYGRAPH_PY313_LOAD_SUPER_ATTR;
    result.method_super_attribute_name_shift =
        CPYGRAPH_PY313_SUPER_ATTRIBUTE_NAME_SHIFT;
    result.comparison_opcode = CPYGRAPH_PY313_COMPARE_OP;
    result.comparison_argument_shift =
        CPYGRAPH_PY313_COMPARE_ARGUMENT_SHIFT;
    result.import_star_intrinsic_opcode = CPYGRAPH_PY313_CALL_INTRINSIC_1;
    result.import_star_intrinsic_operand =
        CPYGRAPH_PY313_INTRINSIC_IMPORT_STAR;
    result.clear_local_opcodes = {CPYGRAPH_PY313_LOAD_FAST_AND_CLEAR};
    result.preserve_argument_opcodes = {CPYGRAPH_PY313_COPY, CPYGRAPH_PY313_SWAP};
    result.jump_cache_adjusted_opcodes = {
        CPYGRAPH_PY313_FOR_ITER, CPYGRAPH_PY313_JUMP_BACKWARD,
        CPYGRAPH_PY313_POP_JUMP_IF_FALSE, CPYGRAPH_PY313_POP_JUMP_IF_TRUE,
        CPYGRAPH_PY313_POP_JUMP_IF_NONE, CPYGRAPH_PY313_POP_JUMP_IF_NOT_NONE,
        CPYGRAPH_PY313_SEND};
    result.combined_local_opcodes = {
        CPYGRAPH_PY313_LOAD_FAST_LOAD_FAST, CPYGRAPH_PY313_STORE_FAST_LOAD_FAST,
        CPYGRAPH_PY313_STORE_FAST_STORE_FAST};
    result.conditional_stack_rules = {
        {CPYGRAPH_PY313_FOR_ITER, 0, 1, 0, 1, 1},
        {CPYGRAPH_PY313_SEND, 1, 1, 1, 1, 0},
    };
    result.expanded_call_opcode = CPYGRAPH_PY313_CALL_FUNCTION_EX;
    result.keyword_call_opcode = CPYGRAPH_PY313_CALL_KW;
    result.normalize_create_function = true;
    result.cell_reference_local_opcode = CPYGRAPH_PY313_LOAD_FAST;
    return result;
}
}  // namespace

Python313Adapter::Python313Adapter()
    : TableDrivenCPythonAdapter({
          {CPYGRAPH_PY313_BEFORE_ASYNC_WITH, protocolStack(1U, 2U, P::DynamicProtocol, pythonMethod(M::AsyncEnter) | pythonMethod(M::AsyncExit))},
          {CPYGRAPH_PY313_BEFORE_WITH, fixedStack(O::EnterContext, 1, 2)},
          {CPYGRAPH_PY313_BINARY_SLICE, fixedStack(O::Generic, 3, 1)},
          {CPYGRAPH_PY313_BINARY_SUBSCR, {O::LoadElement}},
          {CPYGRAPH_PY313_CHECK_EG_MATCH, fixedStack(O::Generic, 2, 2)},     // CHECK_EG_MATCH
          {CPYGRAPH_PY313_CHECK_EXC_MATCH, fixedStack(O::Generic, 2, 2)},
          {CPYGRAPH_PY313_CLEANUP_THROW, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY313_DELETE_SUBSCR, {O::DeleteElement}},
          {CPYGRAPH_PY313_END_ASYNC_FOR, fixedStack(O::Generic, 2, 0)},
          {CPYGRAPH_PY313_END_FOR, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY313_END_SEND, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY313_FORMAT_SIMPLE, unaryProtocol(P::DynamicProtocol, M::Format)},
          {CPYGRAPH_PY313_FORMAT_WITH_SPEC, protocolStack(2U, 1U, P::DynamicProtocol, pythonMethod(M::Format))},
          {CPYGRAPH_PY313_GET_AITER, unaryProtocol(P::Iteration, M::AsyncIter)},
          {CPYGRAPH_PY313_GET_ANEXT, peekProtocol(1U, 1U, P::Iteration, pythonMethod(M::AsyncNext))},
          {CPYGRAPH_PY313_GET_ITER, iterationProtocol()},
          {CPYGRAPH_PY313_GET_LEN, peekProtocol(1U, 1U, P::DynamicProtocol, pythonMethod(M::Len))},
          {CPYGRAPH_PY313_GET_YIELD_FROM_ITER, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY313_LOAD_ASSERTION_ERROR, {O::LoadLiteral}},
          {CPYGRAPH_PY313_LOAD_BUILD_CLASS, fixedStack(O::Generic, 0, 1)},
          {CPYGRAPH_PY313_LOAD_LOCALS, fixedStack(O::Generic, 0, 1)},
          {CPYGRAPH_PY313_MAKE_FUNCTION, {O::CreateFunction}},
          {CPYGRAPH_PY313_MATCH_KEYS, fixedStack(O::Generic, 0, 1, 2)}, // MATCH_KEYS
          {CPYGRAPH_PY313_MATCH_MAPPING, fixedStack(O::Generic, 0, 1, 1)}, // MATCH_MAPPING
          {CPYGRAPH_PY313_MATCH_SEQUENCE, fixedStack(O::Generic, 0, 1, 1)}, // MATCH_SEQUENCE
          {CPYGRAPH_PY313_NOP, {O::Nop}},                        // NOP
          {CPYGRAPH_PY313_POP_EXCEPT, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY313_POP_TOP, {O::Pop}},
          {CPYGRAPH_PY313_PUSH_EXC_INFO, fixedStack(O::Generic, 1, 2)},
          {CPYGRAPH_PY313_PUSH_NULL, {O::CallProtocolMarker}},         // PUSH_NULL
          {CPYGRAPH_PY313_RETURN_GENERATOR, {O::LoadLiteral}},               // RETURN_GENERATOR resume input
          {CPYGRAPH_PY313_RETURN_VALUE, {O::Return}},
          {CPYGRAPH_PY313_SETUP_ANNOTATIONS, {O::Nop}},
          {CPYGRAPH_PY313_STORE_SLICE, fixedStack(O::Generic, 4, 0)},
          {CPYGRAPH_PY313_STORE_SUBSCR, {O::StoreElement}},
          {CPYGRAPH_PY313_TO_BOOL, preservingStack(1, 1)},
          {CPYGRAPH_PY313_UNARY_INVERT, unaryProtocol(P::Invert, M::Invert)},
          {CPYGRAPH_PY313_UNARY_NEGATIVE, unaryProtocol(P::Negative, M::Negative)},
          {CPYGRAPH_PY313_UNARY_NOT, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY313_WITH_EXCEPT_START, fixedStack(O::Generic, 0, 1, 4)},
          {CPYGRAPH_PY313_BINARY_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY313_BUILD_CONST_KEY_MAP, collectionResult(StackEffect::ConstKeyMapToOne)},
          {CPYGRAPH_PY313_BUILD_LIST, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY313_BUILD_MAP, collectionResult(StackEffect::MapToOne)},
          {CPYGRAPH_PY313_BUILD_SET, collectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY313_BUILD_SLICE, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY313_BUILD_STRING, dynamicStack(O::Generic, StackEffect::CountToOne, true)},
          {CPYGRAPH_PY313_BUILD_TUPLE, positionalCollectionResult(StackEffect::CountToOne)},
          {CPYGRAPH_PY313_CALL, {O::Call, S::Count}},
          {CPYGRAPH_PY313_CALL_FUNCTION_EX, {O::Call, S::Count}},
          {CPYGRAPH_PY313_CALL_INTRINSIC_1, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY313_CALL_INTRINSIC_2, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY313_CALL_KW, {O::Call, S::Count}},             // CALL_KW
          {CPYGRAPH_PY313_COMPARE_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY313_CONTAINS_OP, protocolStack(2U, 1U, P::DynamicProtocol, pythonMethod(M::Contains))},
          {CPYGRAPH_PY313_CONVERT_VALUE, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY313_COPY, {O::StackCopy}},
          {CPYGRAPH_PY313_COPY_FREE_VARS, {O::Nop}},
          {CPYGRAPH_PY313_DELETE_ATTR, deleteAttributeProtocol()},
          {CPYGRAPH_PY313_DELETE_DEREF,
           lexicalAccess(O::DeleteLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY313_DELETE_FAST, {O::DeleteLocal, S::Deref}},
          {CPYGRAPH_PY313_DELETE_GLOBAL, {O::DeleteGlobal, S::Name}},
          {CPYGRAPH_PY313_DELETE_NAME, {O::DeleteGlobal, S::Name}},
          {CPYGRAPH_PY313_DICT_MERGE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY313_DICT_UPDATE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY313_IMPORT_FROM, {O::ImportAttribute, S::Name}},
          {CPYGRAPH_PY313_IMPORT_NAME, {O::ImportModule, S::Name}},
          {CPYGRAPH_PY313_IS_OP, fixedStack(O::Generic, 2, 1)},
          {CPYGRAPH_PY313_FOR_ITER, iterationNextProtocol()},
          {CPYGRAPH_PY313_GET_AWAITABLE, unaryProtocol(P::DynamicProtocol, M::Await)},
          {CPYGRAPH_PY313_JUMP_BACKWARD, {O::Branch, S::None, J::RelativeBackward}},
          {CPYGRAPH_PY313_JUMP_BACKWARD_NO_INTERRUPT, {O::Branch, S::None, J::RelativeBackward}},
          {CPYGRAPH_PY313_JUMP_FORWARD, {O::Branch, S::None, J::RelativeForward}},
          {CPYGRAPH_PY313_LIST_APPEND, collectionStore(1)},              // LIST_APPEND
          {CPYGRAPH_PY313_LIST_EXTEND, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY313_LOAD_ATTR, {O::LoadAttribute, S::Name}},
          {CPYGRAPH_PY313_LOAD_CONST, {O::LoadConst, S::Constant}},
          {CPYGRAPH_PY313_LOAD_DEREF,
           lexicalAccess(O::LoadLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY313_LOAD_FAST, {O::LoadLocal, S::Deref}},
          {CPYGRAPH_PY313_LOAD_FAST_AND_CLEAR, {O::LoadLocal, S::Deref}},
          {CPYGRAPH_PY313_LOAD_FAST_CHECK, {O::LoadLocal, S::Deref}},        // LOAD_FAST_CHECK
          {CPYGRAPH_PY313_LOAD_FAST_LOAD_FAST, {O::LoadLocalPair}},
          {CPYGRAPH_PY313_LOAD_FROM_DICT_OR_DEREF, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY313_LOAD_FROM_DICT_OR_GLOBALS, fixedStack(O::Generic, 1, 1)},
          {CPYGRAPH_PY313_LOAD_GLOBAL, {O::LoadGlobal, S::Name}},
          {CPYGRAPH_PY313_LOAD_NAME, {O::LoadGlobal, S::Name}},        // LOAD_NAME
          {CPYGRAPH_PY313_LOAD_SUPER_ATTR, {O::LoadAttribute, S::Name}},
          {CPYGRAPH_PY313_MAKE_CELL,
           lexicalAccess(O::Nop, LexicalAccessKind::CellCreation)},
          {CPYGRAPH_PY313_MAP_ADD, collectionStore(2)},              // MAP_ADD
          {CPYGRAPH_PY313_MATCH_CLASS, fixedStack(O::Generic, 3, 1)},
          {CPYGRAPH_PY313_POP_JUMP_IF_FALSE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY313_POP_JUMP_IF_NONE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY313_POP_JUMP_IF_NOT_NONE, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY313_POP_JUMP_IF_TRUE, {O::ConditionalBranch, S::None, J::RelativeForward, true}},
          {CPYGRAPH_PY313_RAISE_VARARGS, dynamicStack(O::Raise, StackEffect::RaiseArguments)},
          {CPYGRAPH_PY313_RERAISE, fixedStack(O::Raise, 1, 0)},
          {CPYGRAPH_PY313_RETURN_CONST, {O::Return, S::Constant, J::None, false, true}},
          {CPYGRAPH_PY313_SEND, {O::ConditionalBranch, S::None, J::RelativeForward, false}},
          {CPYGRAPH_PY313_SET_ADD, collectionStore(1)},             // SET_ADD
          {CPYGRAPH_PY313_SET_FUNCTION_ATTRIBUTE, {O::SetFunctionAttribute, S::Count}},
          {CPYGRAPH_PY313_SET_UPDATE, fixedStack(O::Generic, 1, 0)},
          {CPYGRAPH_PY313_STORE_ATTR, {O::StoreAttribute, S::Name}},
          {CPYGRAPH_PY313_STORE_DEREF,
           lexicalAccess(O::StoreLocal, LexicalAccessKind::CellValue)},
          {CPYGRAPH_PY313_STORE_FAST, {O::StoreLocal, S::Deref}},
          {CPYGRAPH_PY313_STORE_FAST_LOAD_FAST, {O::StoreLoadLocal}},
          {CPYGRAPH_PY313_STORE_FAST_STORE_FAST, {O::StoreLocalPair}},
          {CPYGRAPH_PY313_STORE_GLOBAL, {O::StoreGlobal, S::Name}},
          {CPYGRAPH_PY313_STORE_NAME, {O::StoreGlobal, S::Name}},      // STORE_NAME
          {CPYGRAPH_PY313_SWAP, {O::StackSwap}},
          {CPYGRAPH_PY313_UNPACK_EX, collectionUnpack(StackEffect::UnpackEx)},
          {CPYGRAPH_PY313_UNPACK_SEQUENCE, collectionUnpack(StackEffect::Unpack)},
          {CPYGRAPH_PY313_YIELD_VALUE, fixedStack(O::Yield, 1, 1)},
          {CPYGRAPH_PY313_RESUME, {O::Nop}},                       // RESUME
      }, CPYGRAPH_PY313_EXTENDED_ARG, true, CPYGRAPH_PY313_CACHE,
         true, features()) {}

}  // namespace cpygraph::bytecode
