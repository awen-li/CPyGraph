#include "api/adapters.h"
#include "api/cfg.h"
#include "api/ddg.h"
#include "bytecode/adapters/python310/adapter.h"
#include "bytecode/adapters/python310/opcodes.h"
#include "bytecode/adapters/python311/adapter.h"
#include "bytecode/adapters/python311/opcodes.h"
#include "bytecode/adapters/python312/adapter.h"
#include "bytecode/adapters/python312/opcodes.h"
#include "bytecode/adapters/python313/adapter.h"
#include "bytecode/adapters/python313/opcodes.h"
#include "bytecode/adapters/python314/adapter.h"
#include "bytecode/adapters/python314/opcodes.h"
#include "bytecode/adapters/comparison_operations.h"
#include "test_support.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

int main() {
    using namespace cpygraph::bytecode;
    const auto raw = WordcodeParser(144).parse({144, 1, 100, 2});
    require(raw.size() == 1 && raw[0].offset == 0 && raw[0].prefix_size == 2 &&
                raw[0].argument == 258,
            "common parser combines EXTENDED_ARG and preserves its jump boundary");
    const auto zero_prefix = WordcodeParser(144).parse({144, 0, 100, 2});
    require(zero_prefix.size() == 1 && zero_prefix[0].offset == 0 &&
                zero_prefix[0].prefix_size == 2,
            "common parser preserves a zero-valued EXTENDED_ARG prefix");
    bool rejected = false;
    try { WordcodeParser(144).parse({100}); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "common parser rejects truncated wordcode");

    CodeMetadata metadata{{"module", "member"}, {"local"}, 3};
    metadata.deref_names = {"cell"};
    metadata.cell_names = {"cell"};
    Python310Adapter py310;
    auto load310 = py310.lift(RawInstruction{0, 116, 1}, metadata);
    require(load310.opcode == SemanticOpcode::LoadGlobal && load310.symbol == "member",
            "3.10 adapter uses the unshifted name index");
    auto jump310 = py310.lift(RawInstruction{2, 114, 5}, metadata);
    require(jump310.jump_target && *jump310.jump_target == 10,
            "3.10 adapter resolves absolute jump code units");
    const auto iter310 = py310.lift(RawInstruction{2, 93, 3}, metadata);
    require(iter310.stack_peek_count == 1 && iter310.stack_input_count == 0 &&
                iter310.stack_output_count == 1 &&
                iter310.jump_stack_input_count == 1 &&
                iter310.jump_stack_output_count == 0,
            "3.10 adapter preserves FOR_ITER's edge-specific stack effects");
    const auto or_pop310 = py310.lift(RawInstruction{2, 111, 3}, metadata);
    require(or_pop310.stack_input_count == 1 &&
                or_pop310.jump_stack_input_count == 0 &&
                or_pop310.stack_peek_count == 1,
            "3.10 adapter preserves JUMP_IF_*_OR_POP's retained jump value");
    const auto setup_with310 = py310.lift(
        RawInstruction{4, CPYGRAPH_PY310_SETUP_WITH, 6}, metadata);
    require(setup_with310.opcode == SemanticOpcode::EnterContext &&
                setup_with310.stack_input_count == 1 &&
                setup_with310.stack_output_count == 2,
            "3.10 adapter retains __exit__ and __enter__ values from SETUP_WITH");
    require(py310.lift(RawInstruction{0, 129, 0}, metadata).opcode ==
                SemanticOpcode::Nop,
            "3.10 adapter does not consume a nonexistent static stack value for GEN_START");
    const auto end_async_for310 = py310.lift(RawInstruction{0, 54, 0}, metadata);
    require(end_async_for310.opcode == SemanticOpcode::Generic &&
                end_async_for310.stack_input_count == 7 &&
                end_async_for310.stack_output_count == 0,
            "3.10 adapter consumes the complete legacy async-for exception state");
    const auto match_mapping310 = py310.lift(RawInstruction{0, 31, 0}, metadata);
    const auto match_keys310 = py310.lift(RawInstruction{2, 33, 0}, metadata);
    require(match_mapping310.opcode == SemanticOpcode::Generic &&
                match_mapping310.stack_peek_count == 1 &&
                match_mapping310.stack_output_count == 1 &&
                match_keys310.opcode == SemanticOpcode::Generic &&
                match_keys310.stack_peek_count == 2 &&
                match_keys310.stack_output_count == 2,
            "3.10 adapter models legacy structural-pattern stack effects");
    require(py310.lift(RawInstruction{0, 10, 0}, metadata).stack_input_count == 1 &&
                py310.lift(RawInstruction{0, 17, 0}, metadata).stack_input_count == 2 &&
                py310.lift(RawInstruction{0, 67, 0}, metadata).stack_output_count == 1 &&
                py310.lift(RawInstruction{0, 70, 0}, metadata).stack_output_count == 0,
            "3.10 adapter covers public unary, in-place, and display operations");
    require(py310.lift(RawInstruction{0, 98, 0}, metadata).opcode ==
                SemanticOpcode::DeleteGlobal &&
                py310.lift(RawInstruction{0, 138, 0}, metadata).opcode ==
                    SemanticOpcode::DeleteLocal &&
                py310.lift(RawInstruction{0, 99, 5}, metadata).operand == 5 &&
                py310.lift(RawInstruction{0, 152, 0}, metadata).stack_output_count == 2,
            "3.10 adapter covers deletion, ROT_N, and MATCH_CLASS");
    const auto legacy_regions = py310.exceptionRegions(
        {122, 3, 100, 0, 1, 0, 110, 0, 83, 0}, metadata);
    require(legacy_regions.size() == 1 && legacy_regions[0].start_offset == 2 &&
                legacy_regions[0].end_offset == 8 &&
                legacy_regions[0].handler_offset == 8 &&
                legacy_regions[0].exception_stack_items == 6,
            "3.10 adapter reconstructs legacy protected regions from SETUP_FINALLY");
    const auto with_regions = py310.exceptionRegions(
        {143, 2, 9, 0, 9, 0, 9, 0}, metadata);
    require(with_regions.size() == 1 && with_regions[0].start_offset == 2 &&
                with_regions[0].end_offset == 6 &&
                with_regions[0].handler_offset == 6 &&
                with_regions[0].exception_stack_items == 6,
            "3.10 adapter reconstructs the SETUP_WITH cleanup stack");
    const auto nested_regions = py310.exceptionRegions(
        {100, 0, 143, 6, 125, 0, 122, 2, 9, 0, 9, 0, 9, 0, 9, 0, 9, 0},
        metadata);
    require(nested_regions.size() == 2 && nested_regions[0].stack_depth == 1 &&
                nested_regions[1].stack_depth == 1,
            "3.10 adapter retains an enclosing with-exit value below a nested handler");

    Python312Adapter py312;
    const auto context312 = py312.lift(
        RawInstruction{0, CPYGRAPH_PY312_BEFORE_WITH, 0}, metadata);
    require(context312.opcode == SemanticOpcode::EnterContext &&
                context312.stack_input_count == 1 &&
                context312.stack_output_count == 2,
            "3.12 adapter identifies context entry semantics");
    auto load312 = py312.lift(RawInstruction{0, 116, 2}, metadata);
    require(load312.opcode == SemanticOpcode::LoadGlobal && load312.symbol == "member",
            "3.12 adapter removes LOAD_GLOBAL flag bit");
    auto jump312 = py312.lift(RawInstruction{2, 114, 3}, metadata);
    require(jump312.jump_target && *jump312.jump_target == 10,
            "3.12 adapter resolves relative jump code units");
    const auto iter312 = py312.lift(RawInstruction{2, 93, 3}, metadata);
    require(iter312.stack_output_count == 1 &&
                iter312.jump_stack_output_count == 1 &&
                iter312.stack_input_count == 0 &&
                iter312.jump_stack_input_count == 0,
            "3.12 adapter preserves FOR_ITER's post-3.11 cleanup protocol");
    const auto super312 = py312.lift(
        RawInstruction{
            0, CPYGRAPH_PY312_LOAD_SUPER_ATTR,
            (1U << CPYGRAPH_PY312_SUPER_ATTRIBUTE_NAME_SHIFT) |
                CPYGRAPH_PY312_SUPER_ATTRIBUTE_METHOD_FLAG},
        metadata);
    require(super312.opcode == SemanticOpcode::LoadAttribute &&
                super312.symbol == "member" && super312.super_attribute &&
                super312.discarded_stack_values == 2U &&
                super312.push_call_receiver && super312.stack_output_count == 2U,
            "3.12 adapter preserves method-form LOAD_SUPER_ATTR call slots");
    const auto compare312 = py312.lift(
        RawInstruction{
            0, CPYGRAPH_PY312_COMPARE_OP,
            CPYGRAPH_COMPARE_EQUAL << CPYGRAPH_PY312_COMPARE_ARGUMENT_SHIFT},
        metadata);
    const auto import_star312 = py312.lift(
        RawInstruction{0, CPYGRAPH_PY312_CALL_INTRINSIC_1,
                       CPYGRAPH_PY312_INTRINSIC_IMPORT_STAR}, metadata);
    require(compare312.operand == CPYGRAPH_COMPARE_EQUAL &&
                compare312.protocol_operation ==
                    PythonProtocolOperation::Comparison &&
                import_star312.protocol_operation ==
                    PythonProtocolOperation::Import,
            "3.12 adapter decodes flagged comparisons and import-star intrinsic");
    require(py312.lift(RawInstruction{0, 143, 0}, metadata).clear_local_after_load,
            "3.12 adapter exposes LOAD_FAST_AND_CLEAR without leaking its opcode");
    require(py312.lift(RawInstruction{0, 32, 0}, metadata).opcode ==
                SemanticOpcode::Generic &&
                py312.lift(RawInstruction{0, 32, 0}, metadata).stack_peek_count == 1,
            "3.12 adapter models MATCH_SEQUENCE");
    require(py312.lift(RawInstruction{0, 98, 0}, metadata).opcode ==
                SemanticOpcode::DeleteGlobal &&
                py312.lift(RawInstruction{0, 139, 0}, metadata).opcode ==
                    SemanticOpcode::DeleteLocal &&
                py312.lift(RawInstruction{0, 175, 0}, metadata).stack_input_count == 1 &&
                py312.lift(RawInstruction{0, 175, 0}, metadata).stack_output_count == 1,
            "3.12 adapter covers namespace deletion and annotation-scope lookup");
    const auto no_caches = py312.lift({100, 0, 0, 0, 83, 0}, metadata);
    require(no_caches.size() == 2 && no_caches[1].offset == 4,
            "3.12 adapter hides inline caches without changing offsets");
    require(AdapterFactory::create("3.10")->version() == "3.10", "adapter factory selects 3.10");
    require(AdapterFactory::create("3.11")->version() == "3.11", "adapter factory selects 3.11");
    require(AdapterFactory::create("3.12")->version() == "3.12", "adapter factory selects 3.12");
    require(AdapterFactory::create("3.13")->version() == "3.13", "adapter factory selects 3.13");
    require(AdapterFactory::create("3.14")->version() == "3.14", "adapter factory selects 3.14");

    Python311Adapter py311;
    const auto context311 = py311.lift(
        RawInstruction{0, CPYGRAPH_PY311_BEFORE_WITH, 0}, metadata);
    require(context311.opcode == SemanticOpcode::EnterContext &&
                context311.stack_input_count == 1 &&
                context311.stack_output_count == 2,
            "3.11 adapter identifies context entry semantics");
    CodeMetadata exception_metadata = metadata;
    exception_metadata.exception_table = {
        130, 10, 13, 0, 140, 1, 46, 0, 141, 15, 31, 3, 156, 2, 46, 0,
        158, 1, 31, 3, 159, 3, 46, 0, 174, 12, 58, 3};
    const auto table_regions = py311.exceptionRegions({}, exception_metadata);
    require(table_regions.size() == 7 && table_regions[0].start_offset == 4 &&
                table_regions[0].end_offset == 24 &&
                table_regions[0].handler_offset == 26 &&
                table_regions[2].stack_depth == 1 && table_regions[2].push_lasti &&
                table_regions[2].exception_stack_items == 2,
            "3.11+ adapter decodes protected ranges, handler depth, and lasti state");
    bool rejected_exception_table = false;
    try {
        exception_metadata.exception_table = {0x40};
        py311.exceptionRegions({}, exception_metadata);
    } catch (const std::invalid_argument&) { rejected_exception_table = true; }
    require(rejected_exception_table,
            "adapter rejects a truncated exception-table varint without reading past input");
    require(py311.lift(RawInstruction{0, 116, 2}, metadata).symbol == "member",
            "3.11 adapter removes the LOAD_GLOBAL flag bit");
    require(py311.lift(RawInstruction{0, 75, 0}, metadata).opcode ==
                SemanticOpcode::LoadLiteral,
            "3.11 adapter materializes the generator resume value consumed by POP_TOP");
    require(py311.lift(RawInstruction{0, 31, 0}, metadata).opcode ==
                SemanticOpcode::Generic &&
                py311.lift(RawInstruction{0, 33, 0}, metadata).stack_peek_count == 2,
            "3.11 adapter models mapping and key structural patterns");
    require(py311.lift(RawInstruction{0, 37, 0}, metadata).stack_input_count == 2 &&
                py311.lift(RawInstruction{0, 37, 0}, metadata).stack_output_count == 2 &&
                py312.lift(RawInstruction{0, 37, 0}, metadata).stack_input_count == 2 &&
                py312.lift(RawInstruction{0, 37, 0}, metadata).stack_output_count == 2,
            "3.11 and 3.12 adapters preserve exception-group match state");
    require(py311.lift(RawInstruction{0, 70, 0}, metadata).stack_output_count == 0 &&
                py311.lift(RawInstruction{0, 88, 0}, metadata).stack_input_count == 2 &&
                py311.lift(RawInstruction{0, 88, 0}, metadata).stack_output_count == 1,
            "3.11 adapter covers display and exception-group preparation");
    require(py311.lift(RawInstruction{2, 140, 1}, metadata).jump_target == 2,
            "3.11 adapter normalizes backward relative jumps");
    const auto send311 = py311.lift(RawInstruction{2, 123, 1}, metadata);
    require(send311.stack_input_count == 1 && send311.stack_output_count == 1 &&
                send311.jump_stack_input_count == 1 &&
                send311.jump_stack_output_count == 0,
            "3.11 adapter preserves SEND's edge-specific stack effects");
    require(py311.lift(RawInstruction{0, 2, 0}, metadata).opcode ==
                SemanticOpcode::CallProtocolMarker &&
            py311.lift(RawInstruction{2, 171, 0}, metadata).call_protocol_input_count == 2 &&
            py311.lift(RawInstruction{4, 116, 1}, metadata).push_call_protocol_marker,
            "3.11 adapter exposes the two-slot call protocol explicitly");
    const auto extended_jump = WordcodeParser(144, 0, true).parse({144, 1, 110, 0});
    require(extended_jump.size() == 1 &&
                py311.lift(extended_jump.front(), metadata).jump_target == 516,
            "relative jumps use the physical opcode after EXTENDED_ARG as their base");

    Python313Adapter py313;
    const auto context313 = py313.lift(
        RawInstruction{0, CPYGRAPH_PY313_BEFORE_WITH, 0}, metadata);
    require(context313.opcode == SemanticOpcode::EnterContext &&
                context313.stack_input_count == 1 &&
                context313.stack_output_count == 2,
            "3.13 adapter identifies context entry semantics");
    require(py313.lift(RawInstruction{0, 91, 2}, metadata).symbol == "member" &&
            py313.lift(RawInstruction{0, 82, 2}, metadata).symbol == "member",
            "3.13 adapter removes name flag bits");
    require(py313.lift(RawInstruction{2, 97, 3}, metadata).jump_target == 12,
            "3.13 adapter includes inline caches in the conditional-jump base");
    require(py313.lift(RawInstruction{4, 82, 1}, metadata).push_call_receiver,
            "3.13 adapter preserves the receiver slot produced by method-form LOAD_ATTR");
    const auto super313 = py313.lift(
        RawInstruction{
            4, CPYGRAPH_PY313_LOAD_SUPER_ATTR,
            (1U << CPYGRAPH_PY313_SUPER_ATTRIBUTE_NAME_SHIFT) |
                CPYGRAPH_PY313_SUPER_ATTRIBUTE_METHOD_FLAG},
        metadata);
    require(super313.opcode == SemanticOpcode::LoadAttribute &&
                super313.symbol == "member" && super313.super_attribute &&
                super313.discarded_stack_values == 2U &&
                super313.push_call_receiver && super313.stack_output_count == 2U,
            "3.13 adapter preserves method-form LOAD_SUPER_ATTR call slots");
    require(py313.lift(RawInstruction{0, 26, 0}, metadata).opcode ==
                SemanticOpcode::CreateFunction,
            "3.13 adapter recognizes renumbered MAKE_FUNCTION");
    require(py313.lift(RawInstruction{0, 20, 0}, metadata).stack_peek_count == 1 &&
                py313.lift(RawInstruction{0, 27, 0}, metadata).stack_peek_count == 2 &&
                py313.lift(RawInstruction{0, 28, 0}, metadata).opcode ==
                    SemanticOpcode::Generic &&
                py313.lift(RawInstruction{0, 29, 0}, metadata).opcode ==
                    SemanticOpcode::Generic,
            "3.13 adapter models renumbered structural-pattern operations");
    require(py313.lift(RawInstruction{0, 6, 0}, metadata).stack_input_count == 2 &&
                py313.lift(RawInstruction{0, 6, 0}, metadata).stack_output_count == 2,
            "3.13 adapter recognizes renumbered CHECK_EG_MATCH");
    require(py313.lift(RawInstruction{0, 66, 0}, metadata).opcode ==
                SemanticOpcode::DeleteGlobal,
            "3.13 adapter recognizes renumbered DELETE_GLOBAL");
    require(py313.lift(RawInstruction{2, 106, 8}, metadata).opcode ==
                SemanticOpcode::SetFunctionAttribute &&
                py313.lift(RawInstruction{2, 106, 8}, metadata).operand == 8,
            "3.13 adapter exposes post-MAKE_FUNCTION closure attachment");
    CodeMetadata cell_only;
    cell_only.deref_names = {"__class__"};
    require(py313.lift(RawInstruction{0, 85, 0}, cell_only).symbol == "__class__",
            "3.13 adapter resolves LOAD_FAST through locals-plus cell slots");
    require(py313.lift(RawInstruction{0, 86, 0}, cell_only).clear_local_after_load,
            "3.13 adapter exposes LOAD_FAST_AND_CLEAR without leaking its opcode");

    Python314Adapter py314;
    require(py314.lift(RawInstruction{0, 92, 2}, metadata).symbol == "member" &&
            py314.lift(RawInstruction{0, 80, 2}, metadata).symbol == "member",
            "3.14 adapter removes name flag bits");
    require(py314.lift(RawInstruction{2, 75, 1}, metadata).jump_target == 4,
            "3.14 adapter includes inline caches in the backward-jump base");
    const auto super314 = py314.lift(
        RawInstruction{
            4, CPYGRAPH_PY314_LOAD_SUPER_ATTR,
            (1U << CPYGRAPH_PY314_SUPER_ATTRIBUTE_NAME_SHIFT) |
                CPYGRAPH_PY314_SUPER_ATTRIBUTE_METHOD_FLAG},
        metadata);
    require(super314.opcode == SemanticOpcode::LoadAttribute &&
                super314.symbol == "member" && super314.super_attribute &&
                super314.discarded_stack_values == 2U &&
                super314.push_call_receiver && super314.stack_output_count == 2U,
            "3.14 adapter preserves method-form LOAD_SUPER_ATTR call slots");
    require(py314.lift(RawInstruction{4, 4, 0}, metadata).operand == 2 &&
                py314.lift(RawInstruction{4, 4, 0}, metadata).call_protocol_input_count == 2,
            "3.14 adapter models fixed args/kwargs CALL_FUNCTION_EX inputs");
    require(py314.lift(RawInstruction{0, 23, 0}, metadata).opcode ==
                SemanticOpcode::CreateFunction,
            "3.14 adapter recognizes renumbered MAKE_FUNCTION");
    require(py314.lift(RawInstruction{0, 24, 0}, metadata).stack_peek_count == 2 &&
                py314.lift(RawInstruction{0, 25, 0}, metadata).opcode ==
                    SemanticOpcode::Generic &&
                py314.lift(RawInstruction{0, 26, 0}, metadata).opcode ==
                    SemanticOpcode::Generic,
            "3.14 adapter models renumbered structural-pattern operations");
    require(py314.lift(RawInstruction{0, 5, 0}, metadata).stack_input_count == 2 &&
                py314.lift(RawInstruction{0, 5, 0}, metadata).stack_output_count == 2,
            "3.14 adapter recognizes renumbered CHECK_EG_MATCH");
    require(py314.lift(RawInstruction{0, 2, 0}, metadata).stack_input_count == 2 &&
                py314.lift(RawInstruction{0, 45, 0}, metadata).stack_input_count == 2 &&
                py314.lift(RawInstruction{0, 45, 1}, metadata).stack_input_count == 3 &&
                py314.lift(RawInstruction{0, 64, 0}, metadata).opcode ==
                    SemanticOpcode::DeleteGlobal,
            "3.14 adapter covers template construction and global deletion");
    const auto subscript314 = py314.lift(RawInstruction{0, 44, 26}, metadata);
    require(subscript314.opcode == SemanticOpcode::LoadElement &&
                subscript314.stack_input_count == 2 &&
                subscript314.stack_output_count == 1,
            "3.14 adapter normalizes BINARY_OP NB_SUBSCR as an element load");
    require(py314.lift(RawInstruction{2, 108, 8}, metadata).opcode ==
                SemanticOpcode::SetFunctionAttribute &&
                py314.lift(RawInstruction{2, 108, 8}, metadata).operand == 8,
            "3.14 adapter exposes post-MAKE_FUNCTION closure attachment");
    cell_only.deref_names = {"__classdict__"};
    require(py314.lift(RawInstruction{0, 86, 0}, cell_only).symbol == "__classdict__",
            "3.14 adapter resolves LOAD_FAST_BORROW through locals-plus cell slots");
    require(py314.lift(RawInstruction{0, 85, 0}, cell_only).clear_local_after_load,
            "3.14 adapter exposes LOAD_FAST_AND_CLEAR without leaking its opcode");
    require(py314.lift(RawInstruction{0, 62, 0}, cell_only).opcode ==
                SemanticOpcode::DeleteLocal &&
                py314.lift(RawInstruction{0, 62, 0}, cell_only).symbol == "__classdict__",
            "3.14 adapter recognizes renumbered DELETE_DEREF");
    const auto special_enter = py314.lift(
        RawInstruction{0, CPYGRAPH_PY314_LOAD_SPECIAL,
                       CPYGRAPH_PY314_SPECIAL_ENTER}, metadata);
    const auto special_exit = py314.lift(
        RawInstruction{0, CPYGRAPH_PY314_LOAD_SPECIAL,
                       CPYGRAPH_PY314_SPECIAL_EXIT}, metadata);
    require(special_enter.opcode == SemanticOpcode::LoadAttribute &&
                special_enter.symbol == "__enter__" &&
                special_enter.push_call_receiver &&
                special_enter.protocol_operation ==
                    PythonProtocolOperation::ContextEnter &&
                containsPythonMethod(special_enter.protocol_methods,
                                     PythonSpecialMethod::Enter) &&
                special_exit.opcode == SemanticOpcode::LoadAttribute &&
                special_exit.symbol == "__exit__" &&
                special_exit.push_call_receiver &&
                special_exit.protocol_operation ==
                    PythonProtocolOperation::ContextExit &&
                containsPythonMethod(special_exit.protocol_methods,
                                     PythonSpecialMethod::Exit),
            "3.14 adapter preserves explicit context special-method loads");

    metadata.deref_names = {"cell"};
    const auto closure_references = std::vector<SemanticInstruction>{
        py310.lift(RawInstruction{0, CPYGRAPH_PY310_LOAD_CLOSURE, 0}, metadata),
        py311.lift(RawInstruction{0, CPYGRAPH_PY311_LOAD_CLOSURE, 0}, metadata),
        py312.lift(RawInstruction{0, CPYGRAPH_PY312_LOAD_CLOSURE, 0}, metadata),
        py313.lift(RawInstruction{0, CPYGRAPH_PY313_LOAD_FAST, 0}, metadata),
        py314.lift(RawInstruction{0, CPYGRAPH_PY314_LOAD_FAST, 0}, metadata),
    };
    require(std::all_of(
                closure_references.begin(), closure_references.end(),
                [](const auto& instruction) {
                    return instruction.opcode == SemanticOpcode::LoadLocal &&
                           instruction.lexical_access ==
                               LexicalAccessKind::CellReference;
                }),
            "every adapter distinguishes a captured cell reference from its value");
    const auto dereference_loads = std::vector<SemanticInstruction>{
        py310.lift(RawInstruction{0, CPYGRAPH_PY310_LOAD_DEREF, 0}, metadata),
        py311.lift(RawInstruction{0, CPYGRAPH_PY311_LOAD_DEREF, 0}, metadata),
        py312.lift(RawInstruction{0, CPYGRAPH_PY312_LOAD_DEREF, 0}, metadata),
        py313.lift(RawInstruction{0, CPYGRAPH_PY313_LOAD_DEREF, 0}, metadata),
        py314.lift(RawInstruction{0, CPYGRAPH_PY314_LOAD_DEREF, 0}, metadata),
    };
    require(std::all_of(
                dereference_loads.begin(), dereference_loads.end(),
                [](const auto& instruction) {
                    return instruction.lexical_access ==
                           LexicalAccessKind::CellValue;
                }),
            "every adapter delegates captured-value semantics to the shared core");
    const auto cell_creations = std::vector<SemanticInstruction>{
        py311.lift(RawInstruction{0, CPYGRAPH_PY311_MAKE_CELL, 0}, metadata),
        py312.lift(RawInstruction{0, CPYGRAPH_PY312_MAKE_CELL, 0}, metadata),
        py313.lift(RawInstruction{0, CPYGRAPH_PY313_MAKE_CELL, 0}, metadata),
        py314.lift(RawInstruction{0, CPYGRAPH_PY314_MAKE_CELL, 0}, metadata),
    };
    require(std::all_of(
                cell_creations.begin(), cell_creations.end(),
                [](const auto& instruction) {
                    return instruction.lexical_access ==
                           LexicalAccessKind::CellCreation;
                }),
            "post-3.10 adapters expose cell creation without version logic in PTA");
    const auto built_list = py310.lift(RawInstruction{
        0U, CPYGRAPH_PY310_BUILD_LIST, 3U}, metadata);
    require(built_list.opcode == SemanticOpcode::BuildCollection &&
            built_list.stack_input_count == 3 && built_list.stack_output_count == 1 &&
            built_list.fresh_result && built_list.positional_collection,
            "adapter exposes argument-sized fresh container construction");
    const auto unpacked = py310.lift(RawInstruction{
        0U, CPYGRAPH_PY310_UNPACK_SEQUENCE, 3U}, metadata);
    require(unpacked.opcode == SemanticOpcode::UnpackCollection &&
                unpacked.stack_input_count == 1U &&
                unpacked.stack_output_count == 3U,
            "adapter exposes positional collection unpacking to shared PTA");
    const auto yielded = py310.lift(RawInstruction{
        0U, CPYGRAPH_PY310_YIELD_VALUE, 0U}, metadata);
    require(yielded.opcode == SemanticOpcode::Yield,
            "adapter distinguishes yielded values from generic suspension");
    const auto collection_updates = std::vector<SemanticInstruction>{
        py310.lift(RawInstruction{0, 145, 2}, metadata),
        py311.lift(RawInstruction{0, 145, 2}, metadata),
        py312.lift(RawInstruction{0, 145, 2}, metadata),
        py313.lift(RawInstruction{0, 80, 2}, metadata),
        py314.lift(RawInstruction{0, 78, 2}, metadata),
    };
    require(std::all_of(collection_updates.begin(), collection_updates.end(),
                [](const auto& instruction) {
                    return instruction.opcode ==
                               SemanticOpcode::StoreCollectionElement &&
                           instruction.operand == 2 &&
                           instruction.stack_input_count == 1;
                }),
            "all adapters normalize comprehension collection updates");
    require(py313.lift(RawInstruction{0, 95, 3}, metadata).stack_input_count == 2 &&
                py314.lift(RawInstruction{0, 98, 3}, metadata).opcode ==
                    SemanticOpcode::StoreCollectionElement,
            "newer adapters preserve map-comprehension key/value updates");
    const auto unpack_ex = py314.lift(RawInstruction{0, 118, 0x0102}, metadata);
    require(unpack_ex.stack_input_count == 1 && unpack_ex.stack_output_count == 4,
            "adapter normalizes extended unpack stack cardinality");
    require(py311.lift(RawInstruction{0, 137, 0}, metadata).symbol == "cell",
            "adapter resolves dereference operands through version-neutral metadata");
    CodeMetadata pair_metadata{{}, {"a", "b", "x", "y"}, 0};
    pair_metadata.deref_names = pair_metadata.locals;
    const auto store_load = py313.lift(RawInstruction{0, 111, 0x21}, pair_metadata);
    require(store_load.opcode == SemanticOpcode::StoreLoadLocal &&
            store_load.symbol == "x" && store_load.secondary_symbol == "b",
            "3.13 adapter decodes combined store/load local operands");
    const auto load_pair = py314.lift(RawInstruction{0, 89, 0x23}, pair_metadata);
    require(load_pair.opcode == SemanticOpcode::LoadLocalPair &&
            load_pair.symbol == "x" && load_pair.secondary_symbol == "y",
            "3.14 adapter decodes combined local-load operands");

    const auto require_supported = [&](const CPythonAdapter& adapter,
                                       const std::vector<std::uint8_t>& opcodes) {
        for (const auto opcode : opcodes)
            require(adapter.lift(RawInstruction{100, opcode, 0}, metadata).opcode !=
                        SemanticOpcode::Unsupported,
                    "frozen-corpus opcode has normalized adapter semantics");
    };
    // These are the raw opcode sets observed by inventory_bytecode_opcodes.py
    // over the frozen 100-package cohort for each interpreter. EXTENDED_ARG is
    // consumed by WordcodeParser and therefore intentionally absent.
    require_supported(py310, {
        1,2,3,4,5,6,9,11,12,15,16,19,20,22,23,24,25,26,27,28,29,30,32,49,50,
        51,52,54,55,56,57,59,60,61,62,63,64,65,66,68,69,71,72,73,74,75,76,77,
        78,79,82,83,84,85,86,87,89,90,91,92,93,94,95,96,97,100,101,102,103,
        104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,121,
        122,124,125,126,129,130,131,132,133,135,136,137,141,142,143,145,146,
        147,148,154,155,156,157,160,161,162,163,164,165});
    require_supported(py311, {
        1,2,9,10,11,12,15,25,30,32,35,36,49,50,51,52,53,54,60,61,68,69,71,
        74,75,82,83,84,85,86,87,89,90,91,92,93,94,95,96,97,98,99,100,101,
        102,103,104,105,106,107,108,109,110,111,112,114,115,116,117,118,119,
        120,122,123,124,125,126,128,129,130,131,132,133,134,135,136,137,138,
        139,140,142,145,146,147,148,149,151,152,155,156,157,160,162,163,164,
        165,166,171,172,173,174,175,176});
    require_supported(py312, {
        1,2,4,5,9,11,12,15,25,26,27,30,31,33,35,36,49,50,51,52,53,54,55,60,
        61,68,69,71,74,75,83,85,87,89,90,91,92,93,94,95,96,97,99,100,101,
        102,103,104,105,106,107,108,109,110,114,115,116,117,118,119,120,121,
        122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,
        140,141,142,143,145,146,147,149,150,151,152,155,156,157,162,163,164,
        165,171,172,173,174,176});
    require_supported(py313, {
        1,2,4,5,7,8,9,10,11,12,14,15,16,18,19,21,23,24,25,26,30,31,32,33,
        34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,
        56,57,58,59,60,61,62,63,64,65,67,68,69,72,73,74,75,76,77,78,79,80,
        81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,
        102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,
        149});
    require_supported(py314, {
        1,4,6,7,8,9,10,12,13,14,15,16,18,19,21,22,23,26,27,28,29,30,31,32,
        33,34,35,36,37,38,39,40,41,42,43,44,46,47,48,49,50,51,52,53,54,55,
        56,57,58,59,60,61,63,65,66,67,68,70,71,72,73,74,75,76,77,78,79,80,
        81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,
        102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,
        119,120,128});

    const auto function310 = py310.lift(RawInstruction{0, 132, 3}, metadata);
    const auto function312 = py312.lift(RawInstruction{0, 132, 3}, metadata);
    require(function310.auxiliary_input_count == 2 && function310.discarded_stack_values == 1,
            "3.10 adapter normalizes MAKE_FUNCTION flags and qualname");
    require(function312.auxiliary_input_count == 2 && function312.discarded_stack_values == 0,
            "3.12 adapter normalizes MAKE_FUNCTION without a qualname stack value");
    const auto import310 = py310.lift(RawInstruction{0, 108, 0}, metadata);
    const auto import312 = py312.lift(RawInstruction{0, 108, 0}, metadata);
    require(import310.discarded_stack_values == 2 && import312.discarded_stack_values == 2,
            "adapters normalize IMPORT_NAME setup operands");
    CodeMetadata import_metadata{{"package.module"}, {}, 2};
    import_metadata.constant_integers = {0, std::nullopt};
    import_metadata.constant_string_tuples = {{}, {"member"}};
    import_metadata.constant_strings = {
        std::nullopt, std::string("package.dynamic")};
    const auto string_constant = py310.lift(
        RawInstruction{0, 100, 1}, import_metadata);
    require(string_constant.constant_string ==
                std::optional<std::string>{"package.dynamic"},
            "adapters retain exact string constants as semantic metadata");
    const auto ordinary_import = py310.lift(
        std::vector<std::uint8_t>{100, 0, 100, 0, 108, 0}, import_metadata);
    const auto from_import = py310.lift(
        std::vector<std::uint8_t>{100, 0, 100, 1, 108, 0}, import_metadata);
    require(!ordinary_import.back().import_fromlist &&
                ordinary_import.back().import_from_names.empty() &&
                from_import.back().import_fromlist &&
                from_import.back().import_from_names ==
                    std::vector<std::string>{"member"},
            "common lifting distinguishes dotted imports from from-imports");
    require(py310.lift(RawInstruction{0, 109, 1}, metadata).opcode == SemanticOpcode::ImportAttribute &&
            py312.lift(RawInstruction{0, 109, 1}, metadata).opcode == SemanticOpcode::ImportAttribute,
            "adapters normalize IMPORT_FROM without consuming its module base");
    const auto return_const = py312.lift(RawInstruction{0, 121, 2}, metadata);
    require(return_const.opcode == SemanticOpcode::Return && return_const.implicit_constant &&
            return_const.operand == 2,
            "3.12 adapter exposes RETURN_CONST through version-independent implicit-constant metadata");

    // The raw jump operands differ (absolute in 3.10, relative in 3.12), but
    // adapters must produce equivalent version-independent programs.
    CodeMetadata equivalent_metadata{{"x"}, {}, 2};
    const std::vector<std::uint8_t> bytes310{
        100, 0, 90, 0, 101, 0, 114, 6, 100, 1, 110, 1, 100, 0, 83, 0};
    const std::vector<std::uint8_t> bytes312{
        100, 0, 90, 0, 101, 0, 114, 2, 100, 1, 110, 1, 100, 0, 83, 0};
    const auto program310 = py310.lift(bytes310, equivalent_metadata);
    const auto program312 = py312.lift(bytes312, equivalent_metadata);
    require(program310.size() == program312.size(), "cross-version programs have equal semantic length");
    for (std::size_t i = 0; i < program310.size(); ++i) {
        require(program310[i].opcode == program312[i].opcode &&
                program310[i].operand == program312[i].operand &&
                program310[i].symbol == program312[i].symbol &&
                program310[i].jump_target == program312[i].jump_target,
                "adapters erase raw version differences from semantic IR");
    }
    const auto cfg310 = cpygraph::cfg::CFGBuilder().build(program310);
    const auto cfg312 = cpygraph::cfg::CFGBuilder().build(program312);
    require(cfg310.blocks().size() == cfg312.blocks().size() &&
            cfg310.edges().size() == cfg312.edges().size(),
            "equivalent versions produce identical CFG topology");
    const auto ddg310 = cpygraph::ddg::DDGBuilder().build(cfg310);
    const auto ddg312 = cpygraph::ddg::DDGBuilder().build(cfg312);
    require(ddg310.nodes().size() == ddg312.nodes().size() &&
            ddg310.edges().size() == ddg312.edges().size(),
            "equivalent versions produce identical DDG shape");
    for (std::size_t i = 0; i < ddg310.edges().size(); ++i) {
        require(ddg310.edges()[i].source == ddg312.edges()[i].source &&
                ddg310.edges()[i].target == ddg312.edges()[i].target &&
                ddg310.edges()[i].kind == ddg312.edges()[i].kind,
                "equivalent versions produce identical DDG dependencies");
    }

    CodeMetadata extended_metadata{{}, {}, 300};
    const auto extended_target = py310.lift(std::vector<std::uint8_t>{
        100, 0,       // LOAD_CONST 0
        113, 2,       // JUMP_ABSOLUTE to the EXTENDED_ARG boundary at byte 4
        144, 1, 100, 0, // EXTENDED_ARG 1; LOAD_CONST 256
        83, 0}, extended_metadata);
    require(extended_target.size() == 4 && extended_target[2].offset == 4,
            "semantic instruction starts at its EXTENDED_ARG jump boundary");
    require(cpygraph::cfg::CFGBuilder().build(extended_target).blocks().size() == 2,
            "CFG accepts a jump target that lands on an EXTENDED_ARG prefix");
}
