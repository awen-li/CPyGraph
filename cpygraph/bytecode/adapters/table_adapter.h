#pragma once

#include "bytecode/adapters/adapter.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define CPYGRAPH_NO_CACHE_OPCODE 0

namespace cpygraph::bytecode {

enum class OperandSource { None, Name, Local, Deref, Constant, Count };
enum class JumpKind { None, Absolute, RelativeForward, RelativeBackward };
enum class StackEffect {
    None,
    Fixed,
    CountToOne,
    MapToOne,
    ConstKeyMapToOne,
    Unpack,
    UnpackEx,
    FormatValue,
    CallFunctionEx,
    RaiseArguments
};

struct OpcodeSemantics {
    SemanticOpcode opcode{SemanticOpcode::Unsupported};
    OperandSource operand_source{OperandSource::None};
    JumpKind jump_kind{JumpKind::None};
    bool jump_on_true{false};
    bool implicit_constant{false};
    StackEffect stack_effect{StackEffect::None};
    std::uint32_t stack_inputs{};
    std::uint32_t stack_outputs{};
    std::uint32_t stack_peeks{};
    bool fresh_result{false};
    bool provenance_preserving{false};
    bool positional_collection{false};
    PythonProtocolOperation protocol_operation{PythonProtocolOperation::None};
    PythonMethodSet protocol_methods{};
    LexicalAccessKind lexical_access{LexicalAccessKind::FastLocal};
};

inline OpcodeSemantics lexicalAccess(SemanticOpcode opcode,
                                     LexicalAccessKind access) {
    OpcodeSemantics result;
    result.opcode = opcode;
    result.operand_source = OperandSource::Deref;
    result.lexical_access = access;
    return result;
}

inline OpcodeSemantics fixedStack(SemanticOpcode opcode, std::uint32_t inputs,
                                  std::uint32_t outputs,
                                  std::uint32_t peeks = 0,
                                  bool fresh_result = false) {
    OpcodeSemantics result;
    result.opcode = opcode;
    result.stack_effect = StackEffect::Fixed;
    result.stack_inputs = inputs;
    result.stack_outputs = outputs;
    result.stack_peeks = peeks;
    result.fresh_result = fresh_result;
    return result;
}

inline OpcodeSemantics dynamicStack(SemanticOpcode opcode, StackEffect effect,
                                    bool fresh_result = false) {
    OpcodeSemantics result;
    result.opcode = opcode;
    result.stack_effect = effect;
    result.fresh_result = fresh_result;
    return result;
}

inline OpcodeSemantics preservingStack(std::uint32_t inputs,
                                       std::uint32_t outputs,
                                       std::uint32_t peeks = 0) {
    auto result = fixedStack(SemanticOpcode::Generic, inputs, outputs, peeks);
    result.provenance_preserving = true;
    return result;
}

inline OpcodeSemantics protocolStack(
    std::uint32_t inputs, std::uint32_t outputs,
    PythonProtocolOperation operation, PythonMethodSet methods) {
    auto result = fixedStack(SemanticOpcode::Generic, inputs, outputs);
    result.protocol_operation = operation;
    result.protocol_methods = methods;
    return result;
}

inline OpcodeSemantics peekProtocol(
    std::uint32_t outputs, std::uint32_t peeks,
    PythonProtocolOperation operation, PythonMethodSet methods) {
    auto result = fixedStack(SemanticOpcode::Generic, 0U, outputs, peeks);
    result.protocol_operation = operation;
    result.protocol_methods = methods;
    return result;
}

inline OpcodeSemantics dynamicProtocol(
    StackEffect effect, PythonProtocolOperation operation,
    PythonMethodSet methods) {
    auto result = dynamicStack(SemanticOpcode::Generic, effect);
    result.protocol_operation = operation;
    result.protocol_methods = methods;
    return result;
}

inline OpcodeSemantics iterationNextProtocol() {
    OpcodeSemantics result{
        SemanticOpcode::ConditionalBranch,
        OperandSource::None,
        JumpKind::RelativeForward,
        false,
    };
    result.protocol_operation = PythonProtocolOperation::Iteration;
    result.protocol_methods = pythonMethod(PythonSpecialMethod::Next);
    return result;
}

inline OpcodeSemantics unaryProtocol(PythonProtocolOperation operation,
                                     PythonSpecialMethod method) {
    return protocolStack(1U, 1U, operation, pythonMethod(method));
}

inline OpcodeSemantics deleteAttributeProtocol() {
    return protocolStack(
        1U, 0U, PythonProtocolOperation::DeleteAttribute,
        pythonMethod(PythonSpecialMethod::DeleteAttribute));
}

inline OpcodeSemantics iterationProtocol() {
    return unaryProtocol(PythonProtocolOperation::Iteration,
                         PythonSpecialMethod::Iter);
}

inline OpcodeSemantics collectionStore(std::uint32_t values) {
    auto result = fixedStack(SemanticOpcode::StoreCollectionElement, values, 0);
    result.operand_source = OperandSource::Count;
    return result;
}

inline OpcodeSemantics collectionResult(StackEffect effect) {
    return dynamicStack(SemanticOpcode::BuildCollection, effect, true);
}

inline OpcodeSemantics positionalCollectionResult(StackEffect effect) {
    auto result = collectionResult(effect);
    result.positional_collection = true;
    return result;
}

inline OpcodeSemantics collectionUnpack(StackEffect effect) {
    return dynamicStack(SemanticOpcode::UnpackCollection, effect);
}

inline void conditionalStack(SemanticInstruction& instruction,
                             std::uint32_t fallthrough_inputs,
                             std::uint32_t fallthrough_outputs,
                             std::uint32_t jump_inputs,
                             std::uint32_t jump_outputs,
                             std::uint32_t peeks = 0) noexcept {
    instruction.stack_input_count = fallthrough_inputs;
    instruction.stack_output_count = fallthrough_outputs;
    instruction.jump_stack_input_count = jump_inputs;
    instruction.jump_stack_output_count = jump_outputs;
    instruction.stack_peek_count = peeks;
    instruction.explicit_conditional_stack_effect = true;
}

struct ConditionalStackRule {
    std::uint8_t opcode{};
    std::uint32_t fallthrough_inputs{};
    std::uint32_t fallthrough_outputs{};
    std::uint32_t jump_inputs{};
    std::uint32_t jump_outputs{};
    std::uint32_t peeks{};
};

enum class ExpandedCallOperand { ArgumentFlags, ArgsAndKeywords };

// Declarative version deltas consumed by the shared adapter core. Adding a
// CPython version should primarily require an opcode table plus this compact
// feature description, not a new lifting algorithm.
struct AdapterFeatures {
    std::uint32_t call_protocol_inputs{1};
    std::optional<std::uint8_t> flagged_load_global;
    std::optional<std::uint8_t> flagged_load_attribute;
    std::optional<std::uint8_t> method_super_attribute;
    std::uint32_t method_super_attribute_name_shift{};
    std::optional<std::uint8_t> binary_op_opcode;
    std::optional<std::uint8_t> comparison_opcode;
    std::uint32_t comparison_argument_shift{};
    std::optional<std::uint8_t> import_star_intrinsic_opcode;
    std::uint32_t import_star_intrinsic_operand{};
    std::vector<std::uint8_t> receiver_opcodes;
    std::vector<std::uint8_t> clear_local_opcodes;
    std::vector<std::uint8_t> preserve_argument_opcodes;
    std::vector<std::uint8_t> jump_cache_adjusted_opcodes;
    std::vector<std::uint8_t> combined_local_opcodes;
    std::vector<ConditionalStackRule> conditional_stack_rules;
    std::optional<std::uint8_t> expanded_call_opcode;
    ExpandedCallOperand expanded_call_operand{ExpandedCallOperand::ArgumentFlags};
    std::optional<std::uint8_t> keyword_call_opcode;
    bool normalize_create_function{false};
    bool create_function_flag_inputs{false};
    std::uint32_t create_function_discarded_inputs{};
    std::optional<std::uint8_t> cell_reference_local_opcode;
};

class TableDrivenCPythonAdapter : public CPythonAdapter {
public:
    using CPythonAdapter::lift;
    SemanticInstruction lift(const RawInstruction& instruction,
                             const CodeMetadata& metadata) const override;
    std::vector<ExceptionRegion> exceptionRegions(
        const std::vector<std::uint8_t>& bytes,
        const CodeMetadata& metadata) const override;

protected:
    TableDrivenCPythonAdapter(std::unordered_map<std::uint8_t, OpcodeSemantics> table,
                             std::uint8_t extended_arg, bool omit_caches,
                             std::uint8_t cache_opcode = 0,
                             bool encoded_exception_table = false,
                             AdapterFeatures features = {});
    WordcodeParser parser() const override;

private:
    std::uint32_t nameIndex(std::uint8_t opcode, std::uint32_t argument) const noexcept;
    std::unordered_map<std::uint8_t, OpcodeSemantics> table_;
    std::uint8_t extended_arg_;
    bool omit_caches_;
    std::uint8_t cache_opcode_;
    bool encoded_exception_table_;
    AdapterFeatures features_;
};

}  // namespace cpygraph::bytecode
