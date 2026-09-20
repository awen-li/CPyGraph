#pragma once

#include "bytecode/semantic_ir.h"

namespace cpygraph::python {

// Facts about one possible receiver object.  Method sets are compact numeric
// identities; no source/debug strings are retained in PTA or graph storage.
struct ReceiverProtocolFacts {
    bool unknown{false};
    bytecode::PythonMethodSet available_methods{};
    bytecode::PythonMethodSet non_deferred_methods{};
};

struct ProtocolMethodSelection {
    bytecode::PythonMethodSet primary_methods{};
    bytecode::PythonMethodSet secondary_methods{};

    bytecode::PythonMethodSet allMethods() const noexcept {
        return primary_methods | secondary_methods;
    }
};

// Implements ordered Python protocol fallback.  The primary receiver is the
// left/unary operand; the secondary receiver is used by reflected operations.
ProtocolMethodSelection selectProtocolMethods(
    bytecode::PythonProtocolOperation operation,
    bytecode::PythonMethodSet candidates,
    const ReceiverProtocolFacts& primary,
    const ReceiverProtocolFacts& secondary = {}) noexcept;

// Implements object/module attribute lookup after PTA establishes whether the
// requested member exists.  __getattr__ is retained only for a possible miss.
ProtocolMethodSelection selectAttributeLoadMethods(
    bytecode::PythonMethodSet candidates,
    const ReceiverProtocolFacts& receiver,
    bool member_exists,
    bool custom_getattribute_may_miss) noexcept;

ProtocolMethodSelection selectAttributeStoreMethods(
    bytecode::PythonMethodSet candidates,
    const ReceiverProtocolFacts& receiver,
    bool member_exists) noexcept;

// A primary arithmetic/comparison implementation can suppress reflected
// fallback only when every return is proven not to produce NotImplemented.
bool mayReturnNotImplemented(
    const bytecode::SemanticProgram& program) noexcept;

// Conservative summary used for the __getattribute__ -> __getattr__ edge.
// A direct delegation to object.__getattribute__ is non-missing when PTA has
// already proved that the requested member exists.
bool mayMissKnownAttribute(
    const bytecode::SemanticProgram& program) noexcept;

// Some hidden protocol calls have side effects but do not make the bytecode
// result unknown. IMPORT_NAME, for example, still returns the resolved module
// object represented by the package model.
bool hasIntrinsicProtocolResult(
    bytecode::SemanticOpcode opcode) noexcept;

}  // namespace cpygraph::python
