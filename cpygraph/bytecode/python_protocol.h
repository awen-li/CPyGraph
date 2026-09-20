#pragma once

#include <cstdint>
#include <string_view>

namespace cpygraph::bytecode {

enum class PythonProtocolOperation : std::uint16_t {
    None,
    DynamicCall,
    DynamicProtocol,
    ContextEnter,
    ContextExit,
    AttributeLoad,
    AttributeStore,
    Import,
    ImportAttribute,
    ItemLoad,
    ItemStore,
    ItemDelete,
    Truth,
    Add,
    And,
    FloorDivide,
    LeftShift,
    MatrixMultiply,
    Multiply,
    Remainder,
    Or,
    Power,
    RightShift,
    Subtract,
    TrueDivide,
    Xor,
    InplaceAdd,
    InplaceAnd,
    InplaceFloorDivide,
    InplaceLeftShift,
    InplaceMatrixMultiply,
    InplaceMultiply,
    InplaceRemainder,
    InplaceOr,
    InplacePower,
    InplaceRightShift,
    InplaceSubtract,
    InplaceTrueDivide,
    InplaceXor,
    Positive,
    Negative,
    Invert,
    DeleteAttribute,
    Iteration,
    Comparison,
};

enum class PythonSpecialMethod : std::uint16_t {
    Import,
    Enter,
    Exit,
    GetAttribute,
    GetAttr,
    GetDescriptor,
    SetAttr,
    SetDescriptor,
    GetItem,
    SetItem,
    DeleteItem,
    Bool,
    Len,
    Add,
    ReflectedAdd,
    InplaceAdd,
    And,
    ReflectedAnd,
    InplaceAnd,
    FloorDivide,
    ReflectedFloorDivide,
    InplaceFloorDivide,
    LeftShift,
    ReflectedLeftShift,
    InplaceLeftShift,
    MatrixMultiply,
    ReflectedMatrixMultiply,
    InplaceMatrixMultiply,
    Multiply,
    ReflectedMultiply,
    InplaceMultiply,
    Remainder,
    ReflectedRemainder,
    InplaceRemainder,
    Or,
    ReflectedOr,
    InplaceOr,
    Power,
    ReflectedPower,
    InplacePower,
    RightShift,
    ReflectedRightShift,
    InplaceRightShift,
    Subtract,
    ReflectedSubtract,
    InplaceSubtract,
    TrueDivide,
    ReflectedTrueDivide,
    InplaceTrueDivide,
    Xor,
    ReflectedXor,
    InplaceXor,
    Positive,
    Negative,
    Invert,
    DeleteAttribute,
    Iter,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AsyncEnter,
    AsyncExit,
    AsyncIter,
    AsyncNext,
    Await,
    Call,
    Contains,
    Float,
    Int,
    Str,
    Format,
    Hash,
    Next,
    Reversed,
    Count,
};

// Numeric identities for external Python runtime callables retained by an
// unresolved edge. Graph storage does not carry names or allocate strings.
enum class PythonExternalCallee : std::uint8_t {
    None,
    Import,
    ImportModule,
    Input,
    Eval,
    Exec,
    Len,
    Map,
    Range,
    Super,
    DictItems,
    StrJoin,
    StrSplit,
};

// Up to eight method identifiers are byte-packed in one word. Protocol
// fallback sets are small, so this covers Python's method surface without
// growing each unresolved graph-group descriptor.
struct PythonMethodSet {
    std::uint64_t encoded{};

    constexpr explicit operator bool() const noexcept {
        return encoded != 0U;
    }
    constexpr bool operator==(const PythonMethodSet& other) const noexcept {
        return encoded == other.encoded;
    }
    constexpr bool operator!=(const PythonMethodSet& other) const noexcept {
        return !(*this == other);
    }
    constexpr PythonMethodSet& operator|=(PythonMethodSet other) noexcept {
        constexpr std::uint16_t kByteBits = 8U;
        constexpr std::uint64_t kByteMask = 0xffU;
        for (std::uint16_t source = 0U; source < 8U; ++source) {
            const auto method = static_cast<std::uint8_t>(
                (other.encoded >> (source * kByteBits)) & kByteMask);
            if (method == 0U) break;
            bool present = false;
            std::uint16_t destination = 0U;
            for (; destination < 8U; ++destination) {
                const auto existing = static_cast<std::uint8_t>(
                    (encoded >> (destination * kByteBits)) & kByteMask);
                if (existing == method) {
                    present = true;
                    break;
                }
                if (existing == 0U) break;
            }
            if (!present && destination < 8U)
                encoded |= static_cast<std::uint64_t>(method)
                           << (destination * kByteBits);
        }
        return *this;
    }
};

constexpr PythonMethodSet operator|(PythonMethodSet first,
                                    PythonMethodSet second) noexcept {
    first |= second;
    return first;
}

constexpr PythonMethodSet pythonMethod(
    PythonSpecialMethod method) noexcept {
    return PythonMethodSet{
        static_cast<std::uint64_t>(method) + 1U};
}

constexpr bool containsPythonMethod(PythonMethodSet methods,
                                    PythonSpecialMethod method) noexcept {
    constexpr std::uint16_t kByteBits = 8U;
    constexpr std::uint64_t kByteMask = 0xffU;
    const auto selected = static_cast<std::uint8_t>(method) + 1U;
    for (std::uint16_t index = 0U; index < 8U; ++index) {
        const auto candidate = static_cast<std::uint8_t>(
            (methods.encoded >> (index * kByteBits)) & kByteMask);
        if (candidate == selected) return true;
        if (candidate == 0U) return false;
    }
    return false;
}

constexpr PythonMethodSet withoutPythonMethod(
    PythonMethodSet methods, PythonSpecialMethod removed) noexcept {
    constexpr std::uint16_t kByteBits = 8U;
    constexpr std::uint64_t kByteMask = 0xffU;
    PythonMethodSet result;
    for (std::uint16_t index = 0U; index < 8U; ++index) {
        const auto candidate = static_cast<std::uint8_t>(
            (methods.encoded >> (index * kByteBits)) & kByteMask);
        if (candidate == 0U) break;
        const auto method = static_cast<PythonSpecialMethod>(candidate - 1U);
        if (method != removed) result |= pythonMethod(method);
    }
    return result;
}

std::string_view protocolOperationName(PythonProtocolOperation operation) noexcept;
std::string_view specialMethodName(PythonSpecialMethod method) noexcept;
bool isReflectedMethod(PythonSpecialMethod method) noexcept;

static_assert(static_cast<std::uint16_t>(PythonSpecialMethod::Count) < 255U);

}  // namespace cpygraph::bytecode
