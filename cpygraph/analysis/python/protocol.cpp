#include "analysis/python/protocol.h"

namespace cpygraph::python {
namespace {

constexpr std::string_view kImportBuiltinName = "__import__";
constexpr std::string_view kImportlibModuleName = "importlib";
constexpr std::string_view kImportModuleAttributeName = "import_module";
constexpr std::string_view kLenBuiltinName = "len";
constexpr std::string_view kMapBuiltinName = "map";
constexpr std::string_view kRangeBuiltinName = "range";
constexpr std::string_view kSuperBuiltinName = "super";
constexpr std::string_view kItemsAttributeName = "items";
constexpr std::string_view kJoinAttributeName = "join";
constexpr std::string_view kSplitAttributeName = "split";

}  // namespace

ProtocolDispatch builtinProtocolDispatch(std::string_view name) noexcept {
    using P = bytecode::PythonProtocolOperation;
    using M = bytecode::PythonSpecialMethod;
    using bytecode::pythonMethod;
    if (name == kImportBuiltinName)
        return {P::DynamicCall, {},
                bytecode::PythonExternalCallee::Import};
    if (name == "input")
        return {P::DynamicCall, {}, bytecode::PythonExternalCallee::Input};
    if (name == "eval")
        return {P::DynamicCall, {}, bytecode::PythonExternalCallee::Eval};
    if (name == "exec")
        return {P::DynamicCall, {}, bytecode::PythonExternalCallee::Exec};
    if (name == "bool")
        return {P::DynamicProtocol,
                pythonMethod(M::Bool) | pythonMethod(M::Len)};
    if (name == "float") return {P::DynamicProtocol, pythonMethod(M::Float)};
    if (name == "format") return {P::DynamicProtocol, pythonMethod(M::Format)};
    if (name == "hash") return {P::DynamicProtocol, pythonMethod(M::Hash)};
    if (name == "int") return {P::DynamicProtocol, pythonMethod(M::Int)};
    if (name == "iter") return {P::Iteration, pythonMethod(M::Iter)};
    if (name == kLenBuiltinName)
        return {P::DynamicProtocol, pythonMethod(M::Len),
                bytecode::PythonExternalCallee::Len};
    if (name == kMapBuiltinName)
        return {P::DynamicCall, {}, bytecode::PythonExternalCallee::Map};
    if (name == kRangeBuiltinName)
        return {P::DynamicCall, {}, bytecode::PythonExternalCallee::Range};
    if (name == kSuperBuiltinName)
        return {P::DynamicCall, {}, bytecode::PythonExternalCallee::Super};
    if (name == "next") return {P::Iteration, pythonMethod(M::Next)};
    if (name == "reversed")
        return {P::Iteration, pythonMethod(M::Reversed)};
    if (name == "str") return {P::DynamicProtocol, pythonMethod(M::Str)};
    return {P::DynamicCall, {}};
}

bytecode::PythonExternalCallee builtinAttributeCallee(
    std::string_view name) noexcept {
    using E = bytecode::PythonExternalCallee;
    if (name == kItemsAttributeName) return E::DictItems;
    if (name == kJoinAttributeName) return E::StrJoin;
    if (name == kSplitAttributeName) return E::StrSplit;
    return E::None;
}

bytecode::PythonExternalCallee moduleAttributeCallee(
    std::string_view module, std::string_view name) noexcept {
    if (module == kImportlibModuleName &&
        name == kImportModuleAttributeName)
        return bytecode::PythonExternalCallee::ImportModule;
    return bytecode::PythonExternalCallee::None;
}

std::string_view externalCalleeQualifiedTarget(
    bytecode::PythonExternalCallee callee) noexcept {
    using E = bytecode::PythonExternalCallee;
    switch (callee) {
        case E::Import: return "builtins.__import__";
        case E::ImportModule: return "importlib.import_module";
        case E::Input: return "builtins.input";
        case E::Eval: return "builtins.eval";
        case E::Exec: return "builtins.exec";
        case E::Len: return "builtins.len";
        case E::Map: return "builtins.map";
        case E::Range: return "builtins.range";
        case E::Super: return "builtins.super";
        case E::DictItems:
        case E::StrJoin:
        case E::StrSplit:
        case E::None: return {};
    }
    return {};
}

ProtocolDispatch protocolDispatch(
    const bytecode::SemanticInstruction& instruction) {
    if (instruction.protocol_operation !=
            bytecode::PythonProtocolOperation::None ||
        static_cast<bool>(instruction.protocol_methods))
        return {instruction.protocol_operation, instruction.protocol_methods};

    using O = bytecode::SemanticOpcode;
    using P = bytecode::PythonProtocolOperation;
    using M = bytecode::PythonSpecialMethod;
    using bytecode::pythonMethod;
    switch (instruction.opcode) {
        case O::EnterContext:
            return {P::ContextEnter, pythonMethod(M::Enter)};
        case O::LoadAttribute:
            return {P::AttributeLoad,
                    pythonMethod(M::GetAttribute) |
                        pythonMethod(M::GetAttr) |
                        pythonMethod(M::GetDescriptor)};
        case O::StoreAttribute:
            return {P::AttributeStore,
                    pythonMethod(M::SetAttr) |
                        pythonMethod(M::SetDescriptor)};
        case O::ImportModule:
            return {P::Import, pythonMethod(M::Import)};
        case O::ImportAttribute:
            return {P::ImportAttribute, pythonMethod(M::GetAttribute)};
        case O::LoadElement:
            return {P::ItemLoad, pythonMethod(M::GetItem)};
        case O::StoreElement:
        case O::StoreCollectionElement:
            return {P::ItemStore, pythonMethod(M::SetItem)};
        case O::DeleteElement:
            return {P::ItemDelete, pythonMethod(M::DeleteItem)};
        case O::ConditionalBranch:
            return {P::Truth, pythonMethod(M::Bool) | pythonMethod(M::Len)};
        case O::Generic:
        case O::BuildCollection:
        case O::UnpackCollection:
            return {P::DynamicProtocol, {}};
        case O::Suspend:
        case O::Yield:
            return {P::None, {}};
        default:
            return {P::DynamicCall, {}};
    }
}

ProtocolFamily protocolFamily(
    const bytecode::SemanticInstruction& instruction) noexcept {
    using O = bytecode::SemanticOpcode;
    switch (instruction.opcode) {
        case O::Generic:
        case O::BuildCollection:
        case O::UnpackCollection:
            // Operators, iteration, formatting, unpacking, class building,
            // context-manager setup/cleanup, awaiting, and pattern matching
            // are normalized as stack-correct generic operations today.
            return ProtocolFamily::DynamicOperation;
        case O::LoadAttribute:
        case O::StoreAttribute:
            return ProtocolFamily::AttributeAccess;
        case O::ImportModule:
        case O::ImportAttribute:
            return ProtocolFamily::Import;
        case O::EnterContext:
            return ProtocolFamily::DynamicOperation;
        case O::LoadElement:
        case O::StoreElement:
        case O::StoreCollectionElement:
        case O::DeleteElement:
            return ProtocolFamily::ItemAccess;
        case O::ConditionalBranch:
            return ProtocolFamily::Truthiness;
        case O::Nop:
        case O::LoadConst:
        case O::LoadLocal:
        case O::StoreLocal:
        case O::LoadGlobal:
        case O::StoreGlobal:
        case O::CreateFunction:
        case O::SetFunctionAttribute:
        case O::Call:
        case O::PrepareKeywordCall:
        case O::CallProtocolMarker:
        case O::LoadLiteral:
        case O::StackCopy:
        case O::StackSwap:
        case O::StackRotate:
        case O::DeleteLocal:
        case O::DeleteGlobal:
        case O::LoadLocalPair:
        case O::StoreLocalPair:
        case O::StoreLoadLocal:
        case O::Raise:
        case O::Return:
        case O::Pop:
        case O::Branch:
        case O::Suspend:
        case O::Yield:
        case O::Unsupported:
            return ProtocolFamily::None;
    }
    return ProtocolFamily::DynamicOperation;
}

}  // namespace cpygraph::python
