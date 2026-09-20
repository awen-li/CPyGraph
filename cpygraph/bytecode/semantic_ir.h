#pragma once

#include "bytecode/python_protocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cpygraph::bytecode {

enum class SemanticOpcode {
    Nop,
    LoadConst,
    LoadLocal,
    StoreLocal,
    LoadGlobal,
    StoreGlobal,
    LoadAttribute,
    StoreAttribute,
    ImportModule,
    ImportAttribute,
    EnterContext,
    CreateFunction,
    SetFunctionAttribute,
    Call,
    PrepareKeywordCall,
    CallProtocolMarker,
    Generic,
    LoadLiteral,
    StackCopy,
    StackSwap,
    StackRotate,
    LoadElement,
    StoreElement,
    StoreCollectionElement,
    DeleteElement,
    DeleteLocal,
    DeleteGlobal,
    LoadLocalPair,
    StoreLocalPair,
    StoreLoadLocal,
    Raise,
    Return,
    Pop,
    Branch,
    ConditionalBranch,
    Suspend,
    Unsupported,
    // Append semantic operations to preserve the stable numeric values used
    // by serialized observations and external result adapters.
    BuildCollection,
    UnpackCollection,
    Yield,
};

// CPython exposes lexical cells through version-specific opcodes. Preserve
// the storage operation independently of the general load/store opcode so
// downstream analyses can model shared capture storage in one common core.
enum class LexicalAccessKind : std::uint8_t {
    FastLocal,
    CellValue,
    CellReference,
    CellCreation,
};

// Numeric identities for constants whose singleton semantics affect Python
// protocol dispatch.  Analyses use this metadata instead of debug strings.
enum class PythonConstantKind : std::uint8_t {
    Other,
    None,
    NotImplemented,
    Boolean,
};

enum class PythonComparisonKind : std::uint8_t {
    None,
    Less,
    LessEqual,
    Equal,
    NotEqual,
    Greater,
    GreaterEqual,
};

struct SemanticInstruction {
    std::uint32_t offset{};
    SemanticOpcode opcode{SemanticOpcode::Unsupported};
    std::uint32_t operand{};
    std::string symbol;
    std::optional<std::uint32_t> jump_target;
    bool jump_on_true{false};
    std::uint32_t discarded_stack_values{};
    std::uint32_t auxiliary_input_count{};
    bool implicit_constant{false};
    std::uint32_t stack_input_count{};
    std::uint32_t stack_output_count{};
    std::uint32_t stack_peek_count{};
    bool fresh_result{false};
    bool provenance_preserving{false};
    bool positional_collection{false};
    std::string secondary_symbol;
    // Conditional branches can have distinct effects on the explicit jump
    // and fallthrough edges.  The ordinary stack counts describe the
    // fallthrough edge; these counts describe the jump edge.
    std::uint32_t jump_stack_input_count{};
    std::uint32_t jump_stack_output_count{};
    bool explicit_conditional_stack_effect{false};
    bool push_call_protocol_marker{false};
    bool push_call_receiver{false};
    bool super_attribute{false};
    std::uint32_t call_protocol_input_count{1};
    bool keyword_arguments{false};
    bool expanded_arguments{false};
    std::vector<std::string> keyword_names;
    // Shared-core description of an implicit Python dispatch. Version
    // adapters preserve this semantic identity while normalizing opcodes.
    PythonProtocolOperation protocol_operation{PythonProtocolOperation::None};
    PythonMethodSet protocol_methods{};
    std::optional<std::int64_t> constant_integer;
    std::optional<std::string> constant_string;
    PythonConstantKind constant_kind{PythonConstantKind::Other};
    PythonComparisonKind comparison_kind{PythonComparisonKind::None};
    std::optional<std::uint32_t> import_level;
    // IMPORT_NAME returns either the named module (from-import) or the
    // top-level package (ordinary dotted import). Adapters recover this from
    // the preceding fromlist constant so downstream analyses stay version
    // independent.
    bool import_fromlist{false};
    // Names decoded from IMPORT_NAME's fromlist constant. Keeping these on
    // the semantic instruction lets package analyses describe imports without
    // correlating later version-specific IMPORT_FROM instruction sequences.
    std::vector<std::string> import_from_names;
    // Some CPython instructions preserve a local on the operand stack while
    // clearing its slot. Adapters expose that behavior here so analyses do
    // not need to know version-specific opcode numbers.
    bool clear_local_after_load{false};
    LexicalAccessKind lexical_access{LexicalAccessKind::FastLocal};
};

inline std::uint32_t conditionalStackInputs(const SemanticInstruction& instruction,
                                            bool jump_edge) noexcept {
    if (!instruction.explicit_conditional_stack_effect) return 1;
    return jump_edge ? instruction.jump_stack_input_count : instruction.stack_input_count;
}

inline std::uint32_t conditionalStackOutputs(const SemanticInstruction& instruction,
                                             bool jump_edge) noexcept {
    if (!instruction.explicit_conditional_stack_effect) return 0;
    return jump_edge ? instruction.jump_stack_output_count : instruction.stack_output_count;
}

struct CodeMetadata {
    std::vector<std::string> names;
    std::vector<std::string> locals;
    std::size_t constant_count{};
    // Indexed like co_consts. Non-string-tuple constants have an empty entry.
    std::vector<std::vector<std::string>> constant_string_tuples;
    std::vector<std::optional<std::string>> constant_strings;
    std::vector<std::optional<std::int64_t>> constant_integers;
    std::vector<PythonConstantKind> constant_kinds;
    // Indexed by the raw oparg used by LOAD/STORE_DEREF.  Entries may be
    // empty when an interpreter reserves an unnamed locals-plus slot.
    std::vector<std::string> deref_names;
    std::vector<std::string> free_names;
    // Opaque CPython exception-table bytes. Only version adapters interpret
    // this encoding; CFG and downstream analyses consume ExceptionRegion.
    std::vector<std::uint8_t> exception_table;
    // Cell variables are separate from free variables because CPython 3.13+
    // lowers the compiler-only LOAD_CLOSURE pseudo-opcode to LOAD_FAST.
    std::vector<std::string> cell_names;
};

using SemanticProgram = std::vector<SemanticInstruction>;

struct ExceptionRegion {
    std::uint32_t start_offset{};
    std::uint32_t end_offset{};
    std::uint32_t handler_offset{};
    std::uint32_t stack_depth{};
    std::uint32_t exception_stack_items{1};
    bool push_lasti{false};
};

}  // namespace cpygraph::bytecode
