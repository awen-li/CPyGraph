#pragma once

#include "bytecode/semantic_ir.h"

namespace cpygraph::python {

// Families of bytecode operations that may invoke Python-level behavior even
// though the instruction is not an explicit CALL. Unknown protocol dispatch
// remains an explicit CG edge until a more precise model resolves it.
enum class ProtocolFamily {
    None,
    DynamicOperation,
    AttributeAccess,
    Import,
    ItemAccess,
    Truthiness,
};

ProtocolFamily protocolFamily(
    const bytecode::SemanticInstruction& instruction) noexcept;

struct ProtocolDispatch {
    bytecode::PythonProtocolOperation operation{
        bytecode::PythonProtocolOperation::DynamicProtocol};
    bytecode::PythonMethodSet candidate_methods{};
    bytecode::PythonExternalCallee external_callee{
        bytecode::PythonExternalCallee::None};
};

ProtocolDispatch protocolDispatch(
    const bytecode::SemanticInstruction& instruction);

// Returns an exact numeric protocol group for builtins whose invocation is
// defined by special-method dispatch. An empty method set is not modeled.
ProtocolDispatch builtinProtocolDispatch(std::string_view name) noexcept;
bytecode::PythonExternalCallee builtinAttributeCallee(
    std::string_view name) noexcept;

// Selects identities that are only valid for one exact imported module.
// Callers must establish the receiver module identity before using this API.
bytecode::PythonExternalCallee moduleAttributeCallee(
    std::string_view module, std::string_view name) noexcept;

// Exact fully-qualified Python identity for a typed external callee. Empty
// means that the identity is receiver-type-specific rather than module-level.
std::string_view externalCalleeQualifiedTarget(
    bytecode::PythonExternalCallee callee) noexcept;

inline bool mayInvokeUserCode(
    const bytecode::SemanticInstruction& instruction) noexcept {
    return protocolFamily(instruction) != ProtocolFamily::None;
}

}  // namespace cpygraph::python
