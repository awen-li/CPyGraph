#include "analysis/python/language_features.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cpygraph::python {
namespace {

constexpr std::size_t kMaximumProtocolMethods = 8U;
constexpr std::uint16_t kPackedMethodBits = 8U;
constexpr std::uint64_t kPackedMethodMask = 0xffU;
constexpr std::string_view kObjectBuiltinName = "object";
constexpr std::string_view kGetAttributeMethodName = "__getattribute__";

using M = bytecode::PythonSpecialMethod;
using P = bytecode::PythonProtocolOperation;

std::array<M, kMaximumProtocolMethods> unpack(
    bytecode::PythonMethodSet methods, std::size_t& count) noexcept {
    std::array<M, kMaximumProtocolMethods> result{};
    count = 0U;
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto encoded = static_cast<std::uint8_t>(
            (methods.encoded >> (index * kPackedMethodBits)) &
            kPackedMethodMask);
        if (encoded == 0U) break;
        result[count++] = static_cast<M>(encoded - 1U);
    }
    return result;
}

bool isInplaceOperation(P operation) noexcept {
    return operation >= P::InplaceAdd && operation <= P::InplaceXor;
}

bool has(bytecode::PythonMethodSet methods, M method) noexcept {
    return bytecode::containsPythonMethod(methods, method);
}

void includeIfAvailable(bytecode::PythonMethodSet& selected, M method,
                        const ReceiverProtocolFacts& receiver) noexcept {
    if (receiver.unknown || has(receiver.available_methods, method))
        selected |= bytecode::pythonMethod(method);
}

bool methodMayDefer(M method,
                    const ReceiverProtocolFacts& receiver) noexcept {
    return receiver.unknown ||
           !has(receiver.non_deferred_methods, method);
}

bool isDefaultGetattributeCall(const bytecode::SemanticProgram& program,
                               std::size_t call_index) noexcept {
    bool loads_default_getattribute = false;
    for (auto cursor = call_index; cursor != 0U;) {
        --cursor;
        const auto& candidate = program[cursor];
        if (candidate.opcode == bytecode::SemanticOpcode::LoadAttribute &&
            candidate.symbol == kGetAttributeMethodName) {
            loads_default_getattribute = true;
            continue;
        }
        if (loads_default_getattribute &&
            candidate.opcode == bytecode::SemanticOpcode::LoadGlobal &&
            candidate.symbol == kObjectBuiltinName)
            return true;
        if (candidate.opcode == bytecode::SemanticOpcode::Call) break;
    }
    return false;
}

}  // namespace

ProtocolMethodSelection selectProtocolMethods(
    P operation, bytecode::PythonMethodSet candidates,
    const ReceiverProtocolFacts& primary,
    const ReceiverProtocolFacts& secondary) noexcept {
    ProtocolMethodSelection selected;
    std::size_t count = 0U;
    const auto methods = unpack(candidates, count);
    if (count == 0U) return selected;

    if (operation == P::Truth) {
        if (primary.unknown) {
            selected.primary_methods = candidates;
        } else if (has(primary.available_methods, M::Bool)) {
            selected.primary_methods = bytecode::pythonMethod(M::Bool);
        } else if (has(primary.available_methods, M::Len)) {
            selected.primary_methods = bytecode::pythonMethod(M::Len);
        }
        return selected;
    }

    // DynamicProtocol is used for compound runtime operations whose methods
    // are independent callbacks, such as async context enter/exit. They are
    // not an ordered reflected-fallback chain.
    if (operation == P::DynamicProtocol) {
        for (std::size_t index = 0U; index < count; ++index)
            includeIfAvailable(selected.primary_methods, methods[index],
                               primary);
        return selected;
    }

    if (operation == P::Comparison) {
        const auto direct = methods[0];
        const auto reflected = count > 1U ? methods[1] : direct;
        includeIfAvailable(selected.primary_methods, direct, primary);
        const bool use_reflected = primary.unknown ||
            !has(primary.available_methods, direct) ||
            methodMayDefer(direct, primary);
        if (use_reflected)
            includeIfAvailable(selected.secondary_methods, reflected,
                               secondary);
        return selected;
    }

    if (isInplaceOperation(operation) && count > 1U) {
        const auto inplace = methods[0];
        includeIfAvailable(selected.primary_methods, inplace, primary);
        bool continue_dispatch = primary.unknown ||
            !has(primary.available_methods, inplace) ||
            methodMayDefer(inplace, primary);
        if (!continue_dispatch) return selected;

        const auto direct = methods[1];
        includeIfAvailable(selected.primary_methods, direct, primary);
        continue_dispatch = primary.unknown ||
            !has(primary.available_methods, direct) ||
            methodMayDefer(direct, primary);
        if (!continue_dispatch || count < 3U) return selected;
        includeIfAvailable(selected.secondary_methods, methods[2], secondary);
        return selected;
    }

    M direct = methods[0];
    M reflected = M::Count;
    for (std::size_t index = 0U; index < count; ++index) {
        if (bytecode::isReflectedMethod(methods[index])) {
            reflected = methods[index];
            break;
        }
    }
    includeIfAvailable(selected.primary_methods, direct, primary);
    if (reflected != M::Count &&
        (primary.unknown || !has(primary.available_methods, direct) ||
         methodMayDefer(direct, primary)))
        includeIfAvailable(selected.secondary_methods, reflected, secondary);
    return selected;
}

ProtocolMethodSelection selectAttributeLoadMethods(
    bytecode::PythonMethodSet candidates,
    const ReceiverProtocolFacts& receiver, bool member_exists,
    bool custom_getattribute_may_miss) noexcept {
    ProtocolMethodSelection selected;
    if (receiver.unknown) {
        selected.primary_methods = candidates;
        return selected;
    }
    if (has(receiver.available_methods, M::GetAttribute))
        selected.primary_methods |= bytecode::pythonMethod(M::GetAttribute);
    if (bytecode::containsPythonMethod(candidates, M::GetDescriptor) &&
        member_exists)
        selected.primary_methods |= bytecode::pythonMethod(M::GetDescriptor);
    if (has(receiver.available_methods, M::GetAttr) &&
        (!member_exists || custom_getattribute_may_miss))
        selected.primary_methods |= bytecode::pythonMethod(M::GetAttr);
    return selected;
}

ProtocolMethodSelection selectAttributeStoreMethods(
    bytecode::PythonMethodSet candidates,
    const ReceiverProtocolFacts& receiver, bool member_exists) noexcept {
    ProtocolMethodSelection selected;
    if (receiver.unknown) {
        selected.primary_methods = candidates;
        return selected;
    }
    if (has(receiver.available_methods, M::SetAttr))
        selected.primary_methods |= bytecode::pythonMethod(M::SetAttr);
    if (bytecode::containsPythonMethod(candidates, M::SetDescriptor) &&
        member_exists)
        selected.primary_methods |= bytecode::pythonMethod(M::SetDescriptor);
    return selected;
}

bool mayReturnNotImplemented(
    const bytecode::SemanticProgram& program) noexcept {
    bool saw_return = false;
    for (std::size_t index = 0U; index < program.size(); ++index) {
        if (program[index].opcode != bytecode::SemanticOpcode::Return)
            continue;
        saw_return = true;
        if (index == 0U) return true;
        const auto& producer = program[index - 1U];
        if (producer.opcode != bytecode::SemanticOpcode::LoadConst)
            return true;
        if (producer.constant_kind ==
            bytecode::PythonConstantKind::NotImplemented)
            return true;
    }
    return !saw_return;
}

bool mayMissKnownAttribute(
    const bytecode::SemanticProgram& program) noexcept {
    using O = bytecode::SemanticOpcode;
    bool saw_return = false;
    for (std::size_t index = 0U; index < program.size(); ++index) {
        if (program[index].opcode == O::Raise) return true;
        if (program[index].opcode == O::Call &&
            !isDefaultGetattributeCall(program, index))
            return true;
        if (program[index].opcode != O::Return) continue;
        saw_return = true;
        if (index == 0U) return true;
        const auto& producer = program[index - 1U];
        if (producer.opcode == O::LoadConst) continue;
        if (producer.opcode == O::Call &&
            isDefaultGetattributeCall(program, index - 1U))
            continue;
        return true;
    }
    return !saw_return;
}

bool hasIntrinsicProtocolResult(
    bytecode::SemanticOpcode opcode) noexcept {
    return opcode == bytecode::SemanticOpcode::ImportModule;
}

}  // namespace cpygraph::python
