#include "bytecode/python_protocol.h"

namespace cpygraph::bytecode {

std::string_view protocolOperationName(
    PythonProtocolOperation operation) noexcept {
    using P = PythonProtocolOperation;
    switch (operation) {
        case P::None: return "none";
        case P::DynamicCall: return "python.dynamic_call";
        case P::DynamicProtocol: return "python.dynamic_protocol";
        case P::ContextEnter: return "python.context.enter";
        case P::ContextExit: return "python.context.exit";
        case P::AttributeLoad: return "python.attribute.load";
        case P::AttributeStore: return "python.attribute.store";
        case P::Import: return "python.import";
        case P::ImportAttribute: return "python.import.attribute";
        case P::ItemLoad: return "python.item.load";
        case P::ItemStore: return "python.item.store";
        case P::ItemDelete: return "python.item.delete";
        case P::Truth: return "python.truth";
        case P::Add: return "python.operator.add";
        case P::And: return "python.operator.and";
        case P::FloorDivide: return "python.operator.floordiv";
        case P::LeftShift: return "python.operator.lshift";
        case P::MatrixMultiply: return "python.operator.matmul";
        case P::Multiply: return "python.operator.mul";
        case P::Remainder: return "python.operator.mod";
        case P::Or: return "python.operator.or";
        case P::Power: return "python.operator.pow";
        case P::RightShift: return "python.operator.rshift";
        case P::Subtract: return "python.operator.sub";
        case P::TrueDivide: return "python.operator.truediv";
        case P::Xor: return "python.operator.xor";
        case P::InplaceAdd: return "python.operator.iadd";
        case P::InplaceAnd: return "python.operator.iand";
        case P::InplaceFloorDivide: return "python.operator.ifloordiv";
        case P::InplaceLeftShift: return "python.operator.ilshift";
        case P::InplaceMatrixMultiply: return "python.operator.imatmul";
        case P::InplaceMultiply: return "python.operator.imul";
        case P::InplaceRemainder: return "python.operator.imod";
        case P::InplaceOr: return "python.operator.ior";
        case P::InplacePower: return "python.operator.ipow";
        case P::InplaceRightShift: return "python.operator.irshift";
        case P::InplaceSubtract: return "python.operator.isub";
        case P::InplaceTrueDivide: return "python.operator.itruediv";
        case P::InplaceXor: return "python.operator.ixor";
        case P::Positive: return "python.operator.pos";
        case P::Negative: return "python.operator.neg";
        case P::Invert: return "python.operator.invert";
        case P::DeleteAttribute: return "python.attribute.delete";
        case P::Iteration: return "python.iteration";
        case P::Comparison: return "python.comparison";
    }
    return "python.dynamic_protocol";
}

std::string_view specialMethodName(PythonSpecialMethod method) noexcept {
    using M = PythonSpecialMethod;
    switch (method) {
        case M::Import: return "__import__";
        case M::Enter: return "__enter__";
        case M::Exit: return "__exit__";
        case M::GetAttribute: return "__getattribute__";
        case M::GetAttr: return "__getattr__";
        case M::GetDescriptor: return "__get__";
        case M::SetAttr: return "__setattr__";
        case M::SetDescriptor: return "__set__";
        case M::GetItem: return "__getitem__";
        case M::SetItem: return "__setitem__";
        case M::DeleteItem: return "__delitem__";
        case M::Bool: return "__bool__";
        case M::Len: return "__len__";
        case M::Add: return "__add__";
        case M::ReflectedAdd: return "__radd__";
        case M::InplaceAdd: return "__iadd__";
        case M::And: return "__and__";
        case M::ReflectedAnd: return "__rand__";
        case M::InplaceAnd: return "__iand__";
        case M::FloorDivide: return "__floordiv__";
        case M::ReflectedFloorDivide: return "__rfloordiv__";
        case M::InplaceFloorDivide: return "__ifloordiv__";
        case M::LeftShift: return "__lshift__";
        case M::ReflectedLeftShift: return "__rlshift__";
        case M::InplaceLeftShift: return "__ilshift__";
        case M::MatrixMultiply: return "__matmul__";
        case M::ReflectedMatrixMultiply: return "__rmatmul__";
        case M::InplaceMatrixMultiply: return "__imatmul__";
        case M::Multiply: return "__mul__";
        case M::ReflectedMultiply: return "__rmul__";
        case M::InplaceMultiply: return "__imul__";
        case M::Remainder: return "__mod__";
        case M::ReflectedRemainder: return "__rmod__";
        case M::InplaceRemainder: return "__imod__";
        case M::Or: return "__or__";
        case M::ReflectedOr: return "__ror__";
        case M::InplaceOr: return "__ior__";
        case M::Power: return "__pow__";
        case M::ReflectedPower: return "__rpow__";
        case M::InplacePower: return "__ipow__";
        case M::RightShift: return "__rshift__";
        case M::ReflectedRightShift: return "__rrshift__";
        case M::InplaceRightShift: return "__irshift__";
        case M::Subtract: return "__sub__";
        case M::ReflectedSubtract: return "__rsub__";
        case M::InplaceSubtract: return "__isub__";
        case M::TrueDivide: return "__truediv__";
        case M::ReflectedTrueDivide: return "__rtruediv__";
        case M::InplaceTrueDivide: return "__itruediv__";
        case M::Xor: return "__xor__";
        case M::ReflectedXor: return "__rxor__";
        case M::InplaceXor: return "__ixor__";
        case M::Positive: return "__pos__";
        case M::Negative: return "__neg__";
        case M::Invert: return "__invert__";
        case M::DeleteAttribute: return "__delattr__";
        case M::Iter: return "__iter__";
        case M::Equal: return "__eq__";
        case M::NotEqual: return "__ne__";
        case M::Less: return "__lt__";
        case M::LessEqual: return "__le__";
        case M::Greater: return "__gt__";
        case M::GreaterEqual: return "__ge__";
        case M::AsyncEnter: return "__aenter__";
        case M::AsyncExit: return "__aexit__";
        case M::AsyncIter: return "__aiter__";
        case M::AsyncNext: return "__anext__";
        case M::Await: return "__await__";
        case M::Call: return "__call__";
        case M::Contains: return "__contains__";
        case M::Float: return "__float__";
        case M::Int: return "__int__";
        case M::Str: return "__str__";
        case M::Format: return "__format__";
        case M::Hash: return "__hash__";
        case M::Next: return "__next__";
        case M::Reversed: return "__reversed__";
        case M::Count: break;
    }
    return {};
}

bool isReflectedMethod(PythonSpecialMethod method) noexcept {
    using M = PythonSpecialMethod;
    switch (method) {
        case M::ReflectedAdd:
        case M::ReflectedAnd:
        case M::ReflectedFloorDivide:
        case M::ReflectedLeftShift:
        case M::ReflectedMatrixMultiply:
        case M::ReflectedMultiply:
        case M::ReflectedRemainder:
        case M::ReflectedOr:
        case M::ReflectedPower:
        case M::ReflectedRightShift:
        case M::ReflectedSubtract:
        case M::ReflectedTrueDivide:
        case M::ReflectedXor:
            return true;
        default:
            return false;
    }
}

}  // namespace cpygraph::bytecode
