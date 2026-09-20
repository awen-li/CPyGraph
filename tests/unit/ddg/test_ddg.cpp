#include "api/cfg.h"
#include "api/ddg.h"
#include "test_support.h"

#include <stdexcept>

namespace {

cpygraph::ddg::NodeId at(const cpygraph::ddg::DataDependencyGraph& graph,
                          std::uint32_t offset) {
    for (const auto& node : graph.nodes()) if (node.bytecode_offset == offset) return node.id;
    return 0;
}

}

int main() {
    using namespace cpygraph::bytecode;
    SemanticProgram straight{
        {0, SemanticOpcode::LoadGlobal, 0, "source"},
        {2, SemanticOpcode::Call, 0},
        {4, SemanticOpcode::StoreLocal, 0, "x"},
        {6, SemanticOpcode::LoadGlobal, 0, "decode"},
        {8, SemanticOpcode::LoadLocal, 0, "x"},
        {10, SemanticOpcode::Call, 1},
        {12, SemanticOpcode::Return},
    };
    const auto straight_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(straight)));
    require(straight_ddg.hasEdge(at(straight_ddg, 4), at(straight_ddg, 8),
                                 cpygraph::ddg::EdgeKind::DefUse),
            "DDG connects stored local definition to later load");
    require(straight_ddg.hasEdge(at(straight_ddg, 8), at(straight_ddg, 10),
                                 cpygraph::ddg::EdgeKind::Operand),
            "DDG connects actual argument to call");
    require(straight_ddg.hasEdge(at(straight_ddg, 10), at(straight_ddg, 12),
                                 cpygraph::ddg::EdgeKind::ReturnValue),
            "DDG connects call result to return");

    SemanticInstruction transform{4, SemanticOpcode::Generic};
    transform.stack_input_count = 2;
    transform.stack_output_count = 1;
    SemanticProgram transformed{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::LoadConst, 1},
        transform,
        {6, SemanticOpcode::Return},
    };
    const auto transformed_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(transformed)));
    require(transformed_ddg.hasEdge(at(transformed_ddg, 0), at(transformed_ddg, 4),
                                    cpygraph::ddg::EdgeKind::Operand) &&
            transformed_ddg.hasEdge(at(transformed_ddg, 2), at(transformed_ddg, 4),
                                    cpygraph::ddg::EdgeKind::Operand) &&
            transformed_ddg.hasEdge(at(transformed_ddg, 4), at(transformed_ddg, 6),
                                    cpygraph::ddg::EdgeKind::ReturnValue),
            "DDG preserves all dependencies through a normalized value transformation");

    SemanticInstruction build_collection{0, SemanticOpcode::Generic};
    build_collection.stack_output_count = 1;
    build_collection.fresh_result = true;
    SemanticInstruction append_collection{
        4, SemanticOpcode::StoreCollectionElement, 1};
    append_collection.stack_input_count = 1;
    SemanticProgram comprehension{
        build_collection,
        {2, SemanticOpcode::LoadConst, 0},
        append_collection,
        {6, SemanticOpcode::LoadConst, 1},
        {8, SemanticOpcode::LoadElement},
        {10, SemanticOpcode::Return},
    };
    const auto comprehension_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(comprehension)));
    require(comprehension_ddg.hasEdge(at(comprehension_ddg, 2),
                                      at(comprehension_ddg, 4),
                                      cpygraph::ddg::EdgeKind::Operand) &&
                comprehension_ddg.hasEdge(at(comprehension_ddg, 4),
                                          at(comprehension_ddg, 8),
                                          cpygraph::ddg::EdgeKind::Address),
            "DDG separates collection addressing from stored element data");

    SemanticProgram joined{
        {0, SemanticOpcode::LoadGlobal, 0, "condition"},
        {2, SemanticOpcode::ConditionalBranch, 0, "", 10, false},
        {4, SemanticOpcode::LoadConst, 0},
        {6, SemanticOpcode::StoreLocal, 0, "x"},
        {8, SemanticOpcode::Branch, 0, "", 14},
        {10, SemanticOpcode::LoadConst, 1},
        {12, SemanticOpcode::StoreLocal, 0, "x"},
        {14, SemanticOpcode::LoadLocal, 0, "x"},
        {16, SemanticOpcode::Return},
    };
    const auto joined_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(joined)));
    require(joined_ddg.hasEdge(at(joined_ddg, 6), at(joined_ddg, 14),
                               cpygraph::ddg::EdgeKind::DefUse),
            "DDG join retains true-path reaching definition");
    require(joined_ddg.hasEdge(at(joined_ddg, 12), at(joined_ddg, 14),
                               cpygraph::ddg::EdgeKind::DefUse),
            "DDG join retains false-path reaching definition");

    SemanticProgram partial_definition{
        {0, SemanticOpcode::LoadGlobal, 0, "condition"},
        {2, SemanticOpcode::ConditionalBranch, 0, "", 8, false},
        {4, SemanticOpcode::LoadConst, 0},
        {6, SemanticOpcode::StoreLocal, 0, "x"},
        {8, SemanticOpcode::LoadLocal, 0, "x"},
        {10, SemanticOpcode::Return},
    };
    const auto partial_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(partial_definition)));
    cpygraph::ddg::NodeId local_input = 0;
    for (const auto& node : partial_ddg.nodes()) {
        if (node.kind == cpygraph::ddg::NodeKind::Input &&
            partial_ddg.debugString(node.label) == "local x") local_input = node.id;
    }
    require(partial_ddg.hasEdge(at(partial_ddg, 6), at(partial_ddg, 8),
                                cpygraph::ddg::EdgeKind::DefUse) &&
            partial_ddg.hasEdge(local_input, at(partial_ddg, 8),
                                cpygraph::ddg::EdgeKind::DefUse),
            "DDG join retains both the conditional definition and undefined-path input");

    SemanticInstruction load_and_clear{4, SemanticOpcode::LoadLocal, 0, "saved"};
    load_and_clear.clear_local_after_load = true;
    SemanticProgram cleared_local{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::StoreLocal, 0, "saved"},
        load_and_clear,
        {6, SemanticOpcode::Pop},
        {8, SemanticOpcode::LoadLocal, 0, "saved"},
        {10, SemanticOpcode::Return},
    };
    const auto cleared_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(cleared_local)));
    cpygraph::ddg::NodeId cleared_input = 0;
    for (const auto& node : cleared_ddg.nodes()) {
        if (node.kind == cpygraph::ddg::NodeKind::Input &&
            cleared_ddg.debugString(node.label) == "local saved") cleared_input = node.id;
    }
    require(cleared_ddg.hasEdge(at(cleared_ddg, 2), at(cleared_ddg, 4),
                                cpygraph::ddg::EdgeKind::DefUse) &&
            cleared_ddg.hasEdge(cleared_input, at(cleared_ddg, 8),
                                cpygraph::ddg::EdgeKind::DefUse),
            "DDG models load-and-clear and preserves a later unbound read explicitly");

    SemanticProgram namespaces{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::StoreLocal, 0, "same"},
        {4, SemanticOpcode::LoadGlobal, 0, "same"},
        {6, SemanticOpcode::Return},
    };
    const auto namespace_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(namespaces)));
    cpygraph::ddg::NodeId global_input = 0;
    for (const auto& node : namespace_ddg.nodes()) {
        if (node.kind == cpygraph::ddg::NodeKind::Input &&
            namespace_ddg.debugString(node.label) == "global same") global_input = node.id;
    }
    require(namespace_ddg.hasEdge(global_input, at(namespace_ddg, 4),
                                  cpygraph::ddg::EdgeKind::DefUse) &&
            !namespace_ddg.hasEdge(at(namespace_ddg, 2), at(namespace_ddg, 4),
                                   cpygraph::ddg::EdgeKind::DefUse),
            "DDG keeps local and global namespaces distinct");

    SemanticProgram imported{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::LoadConst, 1},
        {4, SemanticOpcode::ImportModule, 0, "package", std::nullopt, false, 2},
        {6, SemanticOpcode::Return},
    };
    const auto import_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(imported)));
    require(import_ddg.hasEdge(at(import_ddg, 0), at(import_ddg, 4),
                               cpygraph::ddg::EdgeKind::Operand) &&
            import_ddg.hasEdge(at(import_ddg, 2), at(import_ddg, 4),
                               cpygraph::ddg::EdgeKind::Operand),
            "DDG records import level and from-list as import operands");

    SemanticProgram implicit_return{
        {0, SemanticOpcode::Return, 0, "", std::nullopt, false, 0, 0, true},
    };
    const auto implicit_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(implicit_return)));
    require(implicit_ddg.edges().size() == 1 &&
            implicit_ddg.edges()[0].kind == cpygraph::ddg::EdgeKind::ReturnValue,
            "DDG represents an adapter-provided implicit return constant explicitly");

    bool rejected_return_underflow = false;
    try {
        cpygraph::ddg::DDGBuilder().build(
            cpygraph::cfg::CFGBuilder().build({{0, SemanticOpcode::Return}}));
    } catch (const std::runtime_error&) { rejected_return_underflow = true; }
    require(rejected_return_underflow,
            "DDG rejects an explicit return without an operand instead of dropping dependency data");

    const auto rejects_stack_operation = [](SemanticOpcode opcode,
                                            std::uint32_t depth) {
        try {
            cpygraph::ddg::DDGBuilder().build(
                cpygraph::cfg::CFGBuilder().build({
                    {0, SemanticOpcode::LoadConst, 0},
                    {2, opcode, depth},
                    {4, SemanticOpcode::Return, 0, "", std::nullopt,
                     false, 0, 0, true},
                }));
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };
    constexpr std::uint32_t kZeroDepth = 0;
    constexpr std::uint32_t kOutOfRangeDepth = 2;
    for (const auto opcode : {SemanticOpcode::StackCopy,
                              SemanticOpcode::StackSwap,
                              SemanticOpcode::StackRotate}) {
        require(rejects_stack_operation(opcode, kZeroDepth),
                "DDG rejects a zero-depth stack operation without invalid indexing");
        require(rejects_stack_operation(opcode, kOutOfRangeDepth),
                "DDG rejects an out-of-range stack operation without invalid indexing");
    }

    SemanticInstruction invalid_peek{2, SemanticOpcode::Generic};
    invalid_peek.stack_peek_count = kOutOfRangeDepth;
    bool rejected_invalid_peek = false;
    try {
        cpygraph::ddg::DDGBuilder().build(
            cpygraph::cfg::CFGBuilder().build({
                {0, SemanticOpcode::LoadConst, 0}, invalid_peek,
                {4, SemanticOpcode::Return, 0, "", std::nullopt,
                 false, 0, 0, true},
            }));
    } catch (const std::runtime_error&) {
        rejected_invalid_peek = true;
    }
    require(rejected_invalid_peek,
            "DDG rejects an out-of-range generic peek without invalid indexing");

    SemanticProgram stack_operations{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::LoadConst, 1},
        {4, SemanticOpcode::StoreAttribute, 0, "field"},
        {6, SemanticOpcode::LoadConst, 2},
        {8, SemanticOpcode::Pop},
        {10, SemanticOpcode::LoadGlobal, 0, "condition"},
        {12, SemanticOpcode::ConditionalBranch, 0, "", 16, false},
        {14, SemanticOpcode::Branch, 0, "", 18},
        {16, SemanticOpcode::Nop},
        {18, SemanticOpcode::Return, 0, "", std::nullopt, false, 0, 0, true},
    };
    const auto operations_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(stack_operations)));
    require(operations_ddg.hasEdge(at(operations_ddg, 0), at(operations_ddg, 4),
                                   cpygraph::ddg::EdgeKind::Operand) &&
            operations_ddg.hasEdge(at(operations_ddg, 2), at(operations_ddg, 4),
                                   cpygraph::ddg::EdgeKind::Address),
            "DDG separates stored values from attribute receiver addresses");
    require(operations_ddg.hasEdge(at(operations_ddg, 6), at(operations_ddg, 8),
                                   cpygraph::ddg::EdgeKind::Operand),
            "DDG records the discarded value as the operand of pop");
    require(operations_ddg.hasEdge(at(operations_ddg, 10), at(operations_ddg, 12),
                                   cpygraph::ddg::EdgeKind::Operand),
            "DDG records the tested value as the conditional-branch operand");

    SemanticInstruction for_iter{2, SemanticOpcode::ConditionalBranch, 0, "", 8};
    for_iter.stack_output_count = 1;
    for_iter.stack_peek_count = 1;
    for_iter.jump_stack_input_count = 1;
    for_iter.explicit_conditional_stack_effect = true;
    SemanticProgram iterated{
        {0, SemanticOpcode::LoadGlobal, 0, "iterator"},
        for_iter,
        {4, SemanticOpcode::StoreLocal, 0, "item"},
        {6, SemanticOpcode::Branch, 0, "", 2},
        {8, SemanticOpcode::Return, 0, "", std::nullopt, false, 0, 0, true},
    };
    const auto iterated_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(iterated)));
    require(iterated_ddg.hasEdge(at(iterated_ddg, 0), at(iterated_ddg, 2),
                                 cpygraph::ddg::EdgeKind::Operand) &&
            iterated_ddg.hasEdge(at(iterated_ddg, 2), at(iterated_ddg, 4),
                                 cpygraph::ddg::EdgeKind::Operand),
            "DDG transfers iterator exhaustion and yielded values on distinct CFG edges");

    SemanticInstruction implicit_call{4, SemanticOpcode::Call, 0};
    implicit_call.call_protocol_input_count = 2;
    SemanticProgram implicit_receiver_call{
        {0, SemanticOpcode::LoadGlobal, 0, "comprehension"},
        {2, SemanticOpcode::LoadGlobal, 0, "iterator"},
        implicit_call,
        {6, SemanticOpcode::Return},
    };
    const auto implicit_call_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(implicit_receiver_call)));
    require(implicit_call_ddg.hasEdge(at(implicit_call_ddg, 0), at(implicit_call_ddg, 4),
                                      cpygraph::ddg::EdgeKind::Callee) &&
            implicit_call_ddg.hasEdge(at(implicit_call_ddg, 2), at(implicit_call_ddg, 4),
                                      cpygraph::ddg::EdgeKind::Operand),
            "DDG distinguishes callable identity from implicit receiver data");

    SemanticInstruction marked_call{4, SemanticOpcode::Call, 0};
    marked_call.call_protocol_input_count = 2;
    SemanticProgram marker_call{
        {0, SemanticOpcode::CallProtocolMarker},
        {2, SemanticOpcode::LoadGlobal, 0, "callable"},
        marked_call,
        {6, SemanticOpcode::Return},
    };
    const auto marker_call_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(marker_call)));
    require(marker_call_ddg.hasEdge(at(marker_call_ddg, 2), at(marker_call_ddg, 4),
                                    cpygraph::ddg::EdgeKind::Callee),
            "DDG consumes an explicit null marker without inventing a data dependency");

    SemanticInstruction keyword_call{6, SemanticOpcode::Call, 1};
    keyword_call.discarded_stack_values = 1;
    SemanticProgram keyword_metadata{
        {0, SemanticOpcode::LoadGlobal, 0, "callable"},
        {2, SemanticOpcode::LoadConst, 0},
        {4, SemanticOpcode::LoadConst, 1},
        keyword_call,
        {8, SemanticOpcode::Return},
    };
    const auto keyword_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(keyword_metadata)));
    require(keyword_ddg.hasEdge(at(keyword_ddg, 0), at(keyword_ddg, 6),
                                cpygraph::ddg::EdgeKind::Callee) &&
            keyword_ddg.hasEdge(at(keyword_ddg, 2), at(keyword_ddg, 6),
                                cpygraph::ddg::EdgeKind::Operand) &&
            !keyword_ddg.hasEdge(at(keyword_ddg, 4), at(keyword_ddg, 6),
                                 cpygraph::ddg::EdgeKind::Operand),
            "DDG discards keyword-name metadata without treating it as call data");

    SemanticProgram imported_attribute{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::LoadConst, 1},
        {4, SemanticOpcode::ImportModule, 0, "package", std::nullopt, false, 2},
        {6, SemanticOpcode::ImportAttribute, 0, "member"},
        {8, SemanticOpcode::StoreLocal, 0, "member"},
        {10, SemanticOpcode::StoreLocal, 0, "module"},
        {12, SemanticOpcode::LoadLocal, 0, "module"},
        {14, SemanticOpcode::LoadAttribute, 0, "other"},
        {16, SemanticOpcode::Return},
    };
    const auto imported_attribute_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(imported_attribute)));
    require(imported_attribute_ddg.hasEdge(at(imported_attribute_ddg, 4),
                                           at(imported_attribute_ddg, 6),
                                           cpygraph::ddg::EdgeKind::Operand),
            "DDG records the module as the from-import attribute operand");
    require(imported_attribute_ddg.hasEdge(at(imported_attribute_ddg, 6),
                                           at(imported_attribute_ddg, 8),
                                           cpygraph::ddg::EdgeKind::Operand) &&
            imported_attribute_ddg.hasEdge(at(imported_attribute_ddg, 4),
                                           at(imported_attribute_ddg, 10),
                                           cpygraph::ddg::EdgeKind::Operand),
            "DDG preserves both imported attribute and module stack values");
    require(imported_attribute_ddg.hasEdge(at(imported_attribute_ddg, 12),
                                           at(imported_attribute_ddg, 14),
                                           cpygraph::ddg::EdgeKind::Address),
            "DDG classifies attribute-load receivers as addresses");

    SemanticProgram create_function{
        {0, SemanticOpcode::LoadConst, 0},       // default tuple
        {2, SemanticOpcode::LoadConst, 1},       // code object
        {4, SemanticOpcode::LoadConst, 2},       // 3.10 qualname metadata
        {6, SemanticOpcode::CreateFunction, 0, "", std::nullopt, false, 1, 1},
        {8, SemanticOpcode::Return},
    };
    const auto function_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build(std::move(create_function)));
    require(function_ddg.hasEdge(at(function_ddg, 0), at(function_ddg, 6),
                                 cpygraph::ddg::EdgeKind::Operand) &&
            function_ddg.hasEdge(at(function_ddg, 2), at(function_ddg, 6),
                                 cpygraph::ddg::EdgeKind::Operand) &&
            !function_ddg.hasEdge(at(function_ddg, 4), at(function_ddg, 6),
                                  cpygraph::ddg::EdgeKind::Operand),
            "DDG records function code/default inputs while excluding normalized qualname metadata");

    constexpr cpygraph::CodeObjectId kBoundaryCode = 7U;
    constexpr std::uint32_t kOneArgument = 1U;
    SemanticInstruction boundary_call{4, SemanticOpcode::Call, kOneArgument};
    boundary_call.call_protocol_input_count = kOneArgument;
    const auto boundary_result = cpygraph::ddg::DDGBuilder().buildWithBoundaries(
        cpygraph::cfg::CFGBuilder().build({
            {0, SemanticOpcode::LoadGlobal, 0, "callee"},
            {2, SemanticOpcode::LoadLocal, 0, "value"},
            boundary_call,
            {6, SemanticOpcode::Return},
        }),
        kBoundaryCode, {"value"});
    require(boundary_result.formal_parameters.size() == kOneArgument &&
                boundary_result.call_boundaries.size() == kOneArgument &&
                boundary_result.return_values.size() == kOneArgument,
            "DDG exposes one compact formal, call, and return boundary");
    const auto& call_boundary = boundary_result.call_boundaries.front();
    require(call_boundary.actual_arguments.size() == kOneArgument &&
                call_boundary.actual_arguments.front().size() == kOneArgument &&
                call_boundary.actual_arguments.front().front() ==
                    at(boundary_result.graph, 2),
            "DDG call boundary preserves the exact reaching actual definition");
    require(boundary_result.formal_parameters.front() != 0U &&
                boundary_result.return_values.front() ==
                    at(boundary_result.graph, 6),
            "DDG boundaries identify formal inputs and return operations");

    constexpr std::uint32_t kTwoResults = 2U;
    SemanticInstruction split_value{2, SemanticOpcode::Generic};
    split_value.stack_input_count = kOneArgument;
    split_value.stack_output_count = kTwoResults;
    const auto split_ddg = cpygraph::ddg::DDGBuilder().build(
        cpygraph::cfg::CFGBuilder().build({
            {0, SemanticOpcode::LoadConst, 0},
            split_value,
            {4, SemanticOpcode::StoreLocal, 0, "first"},
            {6, SemanticOpcode::StoreLocal, 0, "second"},
            {8, SemanticOpcode::Return, 0, "", std::nullopt,
             false, 0, 0, true},
        }));
    cpygraph::ddg::NodeId first_result = 0U;
    cpygraph::ddg::NodeId second_result = 0U;
    for (const auto& node : split_ddg.nodes()) {
        if (split_ddg.debugString(node.label) == "value operation result 0")
            first_result = node.id;
        if (split_ddg.debugString(node.label) == "value operation result 1")
            second_result = node.id;
    }
    require(first_result != 0U && second_result != 0U &&
                split_ddg.hasEdge(first_result, at(split_ddg, 4),
                                  cpygraph::ddg::EdgeKind::Operand) &&
                split_ddg.hasEdge(second_result, at(split_ddg, 6),
                                  cpygraph::ddg::EdgeKind::Operand),
            "DDG keeps distinct semantic slots for a multi-result operation");

    bool rejected_unsupported = false;
    try {
        cpygraph::ddg::DDGBuilder().build(cpygraph::cfg::CFGBuilder().build(
            {{0, SemanticOpcode::Unsupported}}));
    } catch (const std::runtime_error&) { rejected_unsupported = true; }
    require(rejected_unsupported, "DDG rejects unsupported semantic instructions explicitly");

    cpygraph::ddg::DataDependencyGraph artifact;
    const auto actual = artifact.addNode(cpygraph::ddg::NodeKind::Load, "actual", 0);
    const auto formal = artifact.addNode(cpygraph::ddg::NodeKind::Input, "formal", 0);
    const auto alternate_actual = artifact.addNode(cpygraph::ddg::NodeKind::Load,
                                                    "alternate actual", 0);
    const auto returned = artifact.addNode(cpygraph::ddg::NodeKind::Return, "callee return", 2);
    const auto call = artifact.addNode(cpygraph::ddg::NodeKind::Operation, "call", 4);
    artifact = cpygraph::ddg::InterproceduralStitcher().stitch(
        std::move(artifact), {{call, {{actual, alternate_actual}}, {formal}, {returned}}});
    require(artifact.hasEdge(actual, formal, cpygraph::ddg::EdgeKind::DefUse),
            "interprocedural DDG connects actual argument to formal argument");
    require(artifact.hasEdge(alternate_actual, formal, cpygraph::ddg::EdgeKind::DefUse),
            "interprocedural DDG connects every reaching actual definition to one formal");
    require(artifact.hasEdge(returned, call, cpygraph::ddg::EdgeKind::Result),
            "interprocedural DDG connects callee return to caller result");

    bool rejected_stack_join = false;
    try {
        SemanticProgram unequal_stack{
            {0, SemanticOpcode::LoadGlobal, 0, "condition"},
            {2, SemanticOpcode::ConditionalBranch, 0, "", 8, false},
            {4, SemanticOpcode::LoadConst, 0},
            {6, SemanticOpcode::Branch, 0, "", 8},
            {8, SemanticOpcode::Nop},
            {10, SemanticOpcode::LoadConst, 1},
            {12, SemanticOpcode::Return},
        };
        cpygraph::ddg::DDGBuilder().build(
            cpygraph::cfg::CFGBuilder().build(std::move(unequal_stack)));
    } catch (const std::runtime_error&) { rejected_stack_join = true; }
    require(rejected_stack_join,
            "DDG rejects CFG joins whose predecessor operand-stack heights disagree");

    bool rejected_empty_actual = false;
    try {
        cpygraph::ddg::DataDependencyGraph invalid;
        const auto invalid_call = invalid.addNode(cpygraph::ddg::NodeKind::Operation, "call", 0);
        const auto invalid_formal = invalid.addNode(cpygraph::ddg::NodeKind::Input, "formal", 0);
        cpygraph::ddg::InterproceduralStitcher().stitch(
            std::move(invalid), {{invalid_call, {{}}, {invalid_formal}, {}}});
    } catch (const std::invalid_argument&) { rejected_empty_actual = true; }
    require(rejected_empty_actual,
            "interprocedural DDG rejects resolved arguments without reaching definitions");
}
