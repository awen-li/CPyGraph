#include "api/cfg.h"
#include "api/pta.h"
#include "test_support.h"

#include <algorithm>
#include <stdexcept>

int main() {
    using namespace cpygraph::bytecode;
    SemanticProgram program{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::StoreLocal, 0, "object"},
        {4, SemanticOpcode::LoadLocal, 0, "object"},
        {6, SemanticOpcode::LoadAttribute, 0, "callback"},
        {8, SemanticOpcode::Call, 0},
        {10, SemanticOpcode::Return},
    };
    const auto cfg = cpygraph::cfg::CFGBuilder().build(std::move(program));
    cpygraph::AndersenPointerAnalysis pta;
    const auto callback = pta.internField("callback");
    const auto generated = cpygraph::SemanticPTAConstraintBuilder(pta).build(cfg, 77);
    pta.addFieldAddress(generated.allocated_objects[0], callback, 100);
    pta.solve();

    require(generated.value_nodes.size() == 6, "semantic PTA assigns one stable value node per instruction");
    require(pta.pointsTo(generated.value_nodes[2]).count(generated.allocated_objects[0]) == 1,
            "semantic PTA propagates constant allocation through store/load");
    require(pta.pointsTo(generated.value_nodes[3]).count(100) == 1,
            "semantic PTA resolves known attribute field");
    require(pta.pointsTo(generated.value_nodes[3]).count(pta.unknownObject()) == 1,
            "semantic PTA retains descriptor/dynamic-attribute uncertainty");
    require(generated.attribute_load_bases[3] ==
                std::vector<cpygraph::NodeId>{generated.value_nodes[2]},
            "semantic PTA preserves the receiver of an attribute load");
    bool found_explicit_call = false;
    bool found_descriptor_dispatch = false;
    for (const auto& site : generated.call_sites) {
        found_explicit_call |= site.id == generated.value_nodes[4] && site.caller == 77;
        found_descriptor_dispatch |= site.id == generated.value_nodes[3] &&
                                     site.caller == 77;
    }
    require(found_explicit_call,
            "semantic PTA emits explicit call site using merged callee value");
    require(found_descriptor_dispatch,
            "semantic PTA retains implicit descriptor dispatch as a call site");
    require(pta.pointsTo(generated.value_nodes[4]).count(pta.unknownObject()) == 1,
            "unresolved call result remains explicitly unknown");
    require(generated.allocated_objects[4] != 0 &&
            pta.pointsTo(generated.value_nodes[4]).count(
                generated.allocated_objects[4]) == 1,
            "unresolved call result also has a distinct allocation-site summary");
    require(pta.pointsTo(generated.value_nodes[5]) ==
                pta.pointsTo(generated.value_nodes[4]) &&
            generated.return_values ==
                std::vector<cpygraph::NodeId>{generated.value_nodes[5]},
            "return instruction observation exposes its returned PTA may-set");

    const SemanticProgram element_program{
        {0, SemanticOpcode::LoadGlobal, 0, "mapping"},
        {2, SemanticOpcode::LoadConst, 0},
        {4, SemanticOpcode::LoadElement},
        {6, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis element_pta;
    const auto element_result = cpygraph::SemanticPTAConstraintBuilder(
        element_pta).build(
        cpygraph::cfg::CFGBuilder().build(element_program), 81U);
    require(element_result.element_load_bases[2] ==
                std::vector<cpygraph::NodeId>{
                    element_result.value_nodes[0]} &&
                element_result.element_load_keys[2] ==
                    std::vector<cpygraph::NodeId>{
                        element_result.value_nodes[1]},
            "semantic PTA preserves item-load receiver and key roles");

    SemanticInstruction class_call{2, SemanticOpcode::Call};
    class_call.call_protocol_input_count = 1U;
    const SemanticProgram class_construction{
        {0, SemanticOpcode::LoadGlobal, 0, "Class"},
        class_call,
        {4, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis allocation_pta;
    const auto class_object = allocation_pta.newObject();
    const auto shared_instance = allocation_pta.newObject();
    allocation_pta.addFieldAddress(
        class_object, allocation_pta.internField(
                          cpygraph::kPTAClassInstanceFieldName),
        shared_instance);
    cpygraph::SemanticPTAOptions allocation_options;
    allocation_options.allocation_sensitive = true;
    allocation_options.defer_global_unknowns = true;
    const auto allocation_result = cpygraph::SemanticPTAConstraintBuilder(
        allocation_pta, nullptr, {}, false, 0U,
        std::move(allocation_options)).build(
            cpygraph::cfg::CFGBuilder().build(class_construction), 82U);
    allocation_pta.addAddressOf(
        allocation_result.global_inputs.at("Class").front(), class_object);
    allocation_pta.solve();
    require(allocation_pta.pointsTo(allocation_result.value_nodes[1]).count(
                shared_instance) == 0U &&
            allocation_pta.pointsTo(allocation_result.value_nodes[1]).count(
                allocation_result.allocated_objects[1]) == 1U,
            "allocation-sensitive calls do not merge a class-wide instance summary");

    SemanticInstruction make_cell{0, SemanticOpcode::Nop, 0, "callback"};
    make_cell.lexical_access = LexicalAccessKind::CellCreation;
    SemanticInstruction store_cell{4, SemanticOpcode::StoreLocal, 0, "callback"};
    store_cell.lexical_access = LexicalAccessKind::CellValue;
    SemanticInstruction load_cell_reference{
        6, SemanticOpcode::LoadLocal, 0, "callback"};
    load_cell_reference.lexical_access = LexicalAccessKind::CellReference;
    SemanticInstruction load_cell_value{
        10, SemanticOpcode::LoadLocal, 0, "callback"};
    load_cell_value.lexical_access = LexicalAccessKind::CellValue;
    SemanticProgram lexical_parent{
        make_cell,
        {2, SemanticOpcode::LoadConst, 0},
        store_cell,
        load_cell_reference,
        {8, SemanticOpcode::Pop},
        load_cell_value,
        {12, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis lexical_pta;
    const auto parent_cells = cpygraph::SemanticPTAConstraintBuilder(
        lexical_pta).build(
            cpygraph::cfg::CFGBuilder().build(std::move(lexical_parent)), 80);
    SemanticInstruction load_free_value{
        0, SemanticOpcode::LoadLocal, 0, "callback"};
    load_free_value.lexical_access = LexicalAccessKind::CellValue;
    const auto child_cells = cpygraph::SemanticPTAConstraintBuilder(
        lexical_pta).build(
            cpygraph::cfg::CFGBuilder().build(
                {load_free_value, {2, SemanticOpcode::Return}}),
            81, {}, {"callback"});
    lexical_pta.addCopy(parent_cells.lexical_cell_bindings.at("callback"),
                        child_cells.lexical_cell_bindings.at("callback"));
    lexical_pta.solve();
    require(lexical_pta.pointsTo(parent_cells.value_nodes[3]) ==
                lexical_pta.pointsTo(
                    parent_cells.lexical_cell_bindings.at("callback")),
            "LOAD_CLOSURE returns the numeric cell identity rather than its contents");
    require(lexical_pta.pointsTo(parent_cells.value_nodes[5]).count(
                parent_cells.allocated_objects[1]) == 1U &&
            lexical_pta.pointsTo(child_cells.value_nodes[0]).count(
                parent_cells.allocated_objects[1]) == 1U,
            "captured values flow through shared parent and child cell storage");

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
    cpygraph::AndersenPointerAnalysis comprehension_pta;
    const auto comprehension_result =
        cpygraph::SemanticPTAConstraintBuilder(comprehension_pta).build(
            cpygraph::cfg::CFGBuilder().build(std::move(comprehension)), 79);
    comprehension_pta.solve();
    require(comprehension_pta.pointsTo(
                comprehension_result.value_nodes[4]).count(
                    comprehension_result.allocated_objects[1]) == 1,
            "semantic PTA propagates a comprehension value through its retained container");

    SemanticInstruction build_literal{
        4, SemanticOpcode::BuildCollection};
    build_literal.stack_input_count = 2U;
    build_literal.stack_output_count = 1U;
    build_literal.fresh_result = true;
    SemanticProgram collection_literal{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::LoadConst, 1},
        build_literal,
        {6, SemanticOpcode::LoadConst, 2},
        {8, SemanticOpcode::LoadElement},
        {10, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis literal_pta;
    const auto literal_result = cpygraph::SemanticPTAConstraintBuilder(
        literal_pta).build(cpygraph::cfg::CFGBuilder().build(
            std::move(collection_literal)), 83U);
    literal_pta.solve();
    require(literal_pta.pointsTo(literal_result.value_nodes[4]).count(
                literal_result.allocated_objects[0]) == 1U &&
            literal_pta.pointsTo(literal_result.value_nodes[4]).count(
                literal_result.allocated_objects[1]) == 1U,
            "semantic PTA retains every initial collection element");

    SemanticInstruction positional_build{
        4U, SemanticOpcode::BuildCollection};
    positional_build.stack_input_count = 2U;
    positional_build.stack_output_count = 1U;
    positional_build.fresh_result = true;
    positional_build.positional_collection = true;
    SemanticInstruction positional_unpack{
        6U, SemanticOpcode::UnpackCollection};
    positional_unpack.operand = 2U;
    positional_unpack.stack_input_count = 1U;
    positional_unpack.stack_output_count = 2U;
    cpygraph::AndersenPointerAnalysis unpack_pta;
    const auto unpack_result = cpygraph::SemanticPTAConstraintBuilder(
        unpack_pta).build(cpygraph::cfg::CFGBuilder().build({
            {0U, SemanticOpcode::LoadConst, 0U},
            {2U, SemanticOpcode::LoadConst, 1U},
            positional_build,
            positional_unpack,
            {8U, SemanticOpcode::Return},
        }), 85U);
    unpack_pta.solve();
    require(unpack_pta.pointsTo(unpack_result.value_nodes[4]).count(
                unpack_result.allocated_objects[0]) == 1U &&
                unpack_pta.pointsTo(unpack_result.value_nodes[4]).count(
                    unpack_result.allocated_objects[1]) == 0U,
            "semantic PTA preserves the position selected by collection unpacking");

    SemanticInstruction yield_value{2U, SemanticOpcode::Yield};
    yield_value.stack_input_count = 1U;
    yield_value.stack_output_count = 1U;
    cpygraph::AndersenPointerAnalysis yield_pta;
    const auto yield_result = cpygraph::SemanticPTAConstraintBuilder(
        yield_pta).build(cpygraph::cfg::CFGBuilder().build({
            {0U, SemanticOpcode::LoadConst, 0U},
            yield_value,
            {4U, SemanticOpcode::Return},
        }), 86U);
    yield_pta.solve();
    require(yield_result.return_values.size() == 2U &&
                yield_pta.pointsTo(yield_result.return_values.front()).count(
                    yield_result.allocated_objects.front()) == 1U,
            "semantic PTA exposes yielded objects in the callable return summary");

    SemanticInstruction raise_class{2, SemanticOpcode::Raise};
    raise_class.stack_input_count = 1U;
    cpygraph::AndersenPointerAnalysis raise_pta;
    const auto raise_result = cpygraph::SemanticPTAConstraintBuilder(
        raise_pta).build(cpygraph::cfg::CFGBuilder().build({
            {0, SemanticOpcode::LoadGlobal, 0, "ExceptionClass"},
            raise_class,
        }), 84U);
    require(raise_result.call_sites.size() == 1U &&
            raise_result.call_sites.front().callee_value != 0U,
            "raising an exception class exposes its implicit construction call");

    SemanticProgram rebinding{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::LoadConst, 1},
        {4, SemanticOpcode::StoreAttribute, 0, "__code__"},
        {6, SemanticOpcode::Return, 0, "", std::nullopt, false, 0, 0, true},
    };
    cpygraph::AndersenPointerAnalysis rebinding_pta;
    const auto rebinding_result = cpygraph::SemanticPTAConstraintBuilder(
        rebinding_pta).build(
            cpygraph::cfg::CFGBuilder().build(std::move(rebinding)), 78);
    require(rebinding_result.attribute_store_bases[2] ==
                std::vector<cpygraph::NodeId>{rebinding_result.value_nodes[1]} &&
            rebinding_result.attribute_store_values[2] ==
                std::vector<cpygraph::NodeId>{rebinding_result.value_nodes[0]},
            "semantic PTA preserves base and replacement roles for __code__ stores");

    SemanticProgram identities{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::Pop},
        {4, SemanticOpcode::LoadConst, 0},
        {6, SemanticOpcode::Pop},
        {8, SemanticOpcode::LoadConst, 0},
        {10, SemanticOpcode::LoadConst, 1},
        {12, SemanticOpcode::ImportModule, 0, "marshal", std::nullopt, false, 2},
        {14, SemanticOpcode::StoreGlobal, 0, "first"},
        {16, SemanticOpcode::LoadConst, 0},
        {18, SemanticOpcode::LoadConst, 1},
        {20, SemanticOpcode::ImportModule, 0, "marshal", std::nullopt, false, 2},
        {22, SemanticOpcode::StoreGlobal, 0, "second"},
        {24, SemanticOpcode::LoadGlobal, 0, "first"},
        {26, SemanticOpcode::LoadGlobal, 0, "second"},
        {28, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis identity_pta;
    const auto identity_cfg = cpygraph::cfg::CFGBuilder().build(std::move(identities));
    const auto identity_result = cpygraph::SemanticPTAConstraintBuilder(identity_pta).build(identity_cfg, 1);
    identity_pta.solve();
    require(identity_result.import_requests.size() == 2U &&
                identity_result.import_requests.front().module == "marshal" &&
                identity_result.import_requests.front().from_names.empty() &&
                identity_result.import_requests.front().relative_name_valid,
            "semantic PTA exposes version-independent absolute import requests");
    require(identity_pta.pointsTo(identity_result.value_nodes[0]) ==
            identity_pta.pointsTo(identity_result.value_nodes[2]),
            "repeated loads of one constant-pool entry share abstract identity");
    require(identity_pta.pointsTo(identity_result.value_nodes[12]) ==
            identity_pta.pointsTo(identity_result.value_nodes[13]),
            "repeated imports of one module share abstract identity");

    SemanticProgram namespaces{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::StoreLocal, 0, "name"},
        {4, SemanticOpcode::LoadGlobal, 0, "name"},
        {6, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis namespace_pta;
    const auto namespace_cfg = cpygraph::cfg::CFGBuilder().build(std::move(namespaces));
    const auto namespace_result =
        cpygraph::SemanticPTAConstraintBuilder(namespace_pta).build(namespace_cfg, 1);
    namespace_pta.solve();
    require(namespace_pta.pointsTo(namespace_result.value_nodes[2]).count(
                namespace_pta.unknownObject()) == 1 &&
            namespace_pta.pointsTo(namespace_result.value_nodes[2]).count(
                namespace_result.allocated_objects[0]) == 0,
            "local stores do not incorrectly define same-named globals");

    SemanticProgram from_import{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::LoadConst, 1},
        {4, SemanticOpcode::ImportModule, 0, "package", std::nullopt, false, 2},
        {6, SemanticOpcode::ImportAttribute, 0, "member"},
        {8, SemanticOpcode::StoreLocal, 0, "member"},
        {10, SemanticOpcode::StoreLocal, 0, "module"},
        {12, SemanticOpcode::LoadLocal, 0, "module"},
        {14, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis import_pta;
    const auto import_cfg = cpygraph::cfg::CFGBuilder().build(std::move(from_import));
    const auto import_result = cpygraph::SemanticPTAConstraintBuilder(import_pta).build(import_cfg, 1);
    const auto member_field = import_pta.internField("member");
    import_pta.addFieldAddress(import_result.allocated_objects[2], member_field, 700);
    import_pta.solve();
    require(import_pta.pointsTo(import_result.value_nodes[3]).count(700) == 1,
            "from-import resolves the requested module field");
    require(import_pta.pointsTo(import_result.value_nodes[6]).count(
                import_result.allocated_objects[2]) == 1,
            "from-import leaves the module base on the operand stack");

    SemanticProgram partial_definition{
        {0, SemanticOpcode::LoadGlobal, 0, "condition"},
        {2, SemanticOpcode::ConditionalBranch, 0, "", 8, false},
        {4, SemanticOpcode::LoadConst, 0},
        {6, SemanticOpcode::StoreLocal, 0, "value"},
        {8, SemanticOpcode::LoadLocal, 0, "value"},
        {10, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis partial_pta;
    const auto partial_cfg = cpygraph::cfg::CFGBuilder().build(std::move(partial_definition));
    const auto partial = cpygraph::SemanticPTAConstraintBuilder(partial_pta).build(partial_cfg, 1);
    partial_pta.solve();
    require(partial_pta.pointsTo(partial.value_nodes[4]).count(partial.allocated_objects[2]) == 1 &&
            partial_pta.pointsTo(partial.value_nodes[4]).count(partial_pta.unknownObject()) == 1,
            "semantic PTA retains assigned and unknown alternatives when only one branch defines a local");

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
    cpygraph::AndersenPointerAnalysis cleared_pta;
    cpygraph::SemanticPTAOptions cleared_options;
    cleared_options.flow_sensitive = true;
    const auto cleared = cpygraph::SemanticPTAConstraintBuilder(
        cleared_pta, nullptr, {}, false, 0U, std::move(cleared_options)).build(
        cpygraph::cfg::CFGBuilder().build(std::move(cleared_local)), 1);
    cleared_pta.solve();
    require(cleared_pta.pointsTo(cleared.value_nodes[2]).count(
                cleared.allocated_objects[0]) == 1 &&
            cleared_pta.pointsTo(cleared.value_nodes[4]).count(
                cleared_pta.unknownObject()) == 1 &&
            cleared_pta.pointsTo(cleared.value_nodes[4]).count(
                cleared.allocated_objects[0]) == 0,
            "semantic PTA preserves the loaded value but clears its local binding");

    SemanticProgram selective_flow_program{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::StoreLocal, 0, "selected"},
        {4, SemanticOpcode::LoadLocal, 0, "selected"},
        {6, SemanticOpcode::Pop},
        {8, SemanticOpcode::LoadConst, 1},
        {10, SemanticOpcode::StoreLocal, 0, "selected"},
        {12, SemanticOpcode::LoadLocal, 0, "selected"},
        {14, SemanticOpcode::Return},
    };
    const auto selective_flow_cfg =
        cpygraph::cfg::CFGBuilder().build(selective_flow_program);
    cpygraph::AndersenPointerAnalysis insensitive_pta;
    const auto insensitive = cpygraph::SemanticPTAConstraintBuilder(
        insensitive_pta).build(selective_flow_cfg, 20U);
    insensitive_pta.solve();
    require(insensitive_pta.pointsTo(insensitive.value_nodes[2]).count(
                insensitive.allocated_objects[0]) == 1U &&
            insensitive_pta.pointsTo(insensitive.value_nodes[2]).count(
                insensitive.allocated_objects[4]) == 1U,
            "default PTA merges every assignment to a flow-insensitive local");

    cpygraph::AndersenPointerAnalysis flow_sensitive_pta;
    cpygraph::SemanticPTAOptions flow_options;
    flow_options.flow_sensitive = true;
    const auto flow_sensitive = cpygraph::SemanticPTAConstraintBuilder(
        flow_sensitive_pta, nullptr, {}, false, 0U,
        std::move(flow_options)).build(selective_flow_cfg, 20U);
    flow_sensitive_pta.solve();
    require(flow_sensitive_pta.pointsTo(flow_sensitive.value_nodes[2]).count(
                flow_sensitive.allocated_objects[0]) == 1U &&
            flow_sensitive_pta.pointsTo(flow_sensitive.value_nodes[2]).count(
                flow_sensitive.allocated_objects[4]) == 0U &&
            flow_sensitive_pta.pointsTo(flow_sensitive.value_nodes[6]).count(
                flow_sensitive.allocated_objects[4]) == 1U &&
            flow_sensitive_pta.pointsTo(flow_sensitive.value_nodes[6]).count(
                flow_sensitive.allocated_objects[0]) == 0U,
            "flow-sensitive PTA keeps assignments separated by program point");

    SemanticProgram selective_path_program{
        {0, SemanticOpcode::LoadGlobal, 0, "condition"},
        {2, SemanticOpcode::ConditionalBranch, 0, "", 10},
        {4, SemanticOpcode::LoadConst, 0},
        {6, SemanticOpcode::StoreLocal, 0, "chosen"},
        {8, SemanticOpcode::Branch, 0, "", 14},
        {10, SemanticOpcode::LoadConst, 1},
        {12, SemanticOpcode::StoreLocal, 0, "chosen"},
        {14, SemanticOpcode::LoadLocal, 0, "chosen"},
        {16, SemanticOpcode::Return},
    };
    const auto selective_path_cfg =
        cpygraph::cfg::CFGBuilder().build(selective_path_program);
    cpygraph::AndersenPointerAnalysis path_pta;
    cpygraph::SemanticPTAOptions path_options;
    path_options.flow_sensitive = true;
    path_options.path_decisions.push_back({1U, false});
    const auto selected_path = cpygraph::SemanticPTAConstraintBuilder(
        path_pta, nullptr, {}, false, 0U, std::move(path_options)).build(
            selective_path_cfg, 21U);
    path_pta.solve();
    require(path_pta.pointsTo(selected_path.value_nodes[7]).count(
                selected_path.allocated_objects[2]) == 1U &&
            selected_path.allocated_objects[5] == 0U,
            "path-sensitive PTA materializes only the selected acyclic branch");

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
    cpygraph::AndersenPointerAnalysis iteration_pta;
    const auto iteration_cfg = cpygraph::cfg::CFGBuilder().build(std::move(iterated));
    const auto iteration = cpygraph::SemanticPTAConstraintBuilder(iteration_pta).build(
        iteration_cfg, 9);
    iteration_pta.solve();
    require(iteration_pta.pointsTo(iteration.value_nodes[1]).count(
                iteration_pta.unknownObject()) == 1,
            "semantic PTA transfers the unknown yielded value only to the loop body edge");

    SemanticInstruction implicit_call{4, SemanticOpcode::Call, 0};
    implicit_call.call_protocol_input_count = 2;
    SemanticProgram implicit_receiver_call{
        {0, SemanticOpcode::LoadGlobal, 0, "comprehension"},
        {2, SemanticOpcode::LoadGlobal, 0, "iterator"},
        implicit_call,
        {6, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis call_protocol_pta;
    const auto call_protocol = cpygraph::SemanticPTAConstraintBuilder(call_protocol_pta).build(
        cpygraph::cfg::CFGBuilder().build(std::move(implicit_receiver_call)), 10);
    require(call_protocol.call_sites.size() == 1 &&
            call_protocol.call_arguments.size() == 1 &&
            call_protocol.call_arguments[0].size() == 1 &&
            call_protocol.call_explicit_argument_offsets ==
                std::vector<std::size_t>{1U},
            "semantic PTA exposes the non-null protocol receiver as an implicit argument");
    SemanticProgram marker_argument{
        {0, SemanticOpcode::LoadGlobal, 0, "callee"},
        {2, SemanticOpcode::CallProtocolMarker},
        {4, SemanticOpcode::Call, 1},
        {6, SemanticOpcode::Return},
    };
    const auto conservative_argument =
        cpygraph::SemanticPTAConstraintBuilder(call_protocol_pta).build(
            cpygraph::cfg::CFGBuilder().build(std::move(marker_argument)), 11);
    call_protocol_pta.solve();
    require(conservative_argument.call_arguments.size() == 1 &&
                conservative_argument.call_arguments[0].size() == 1 &&
                conservative_argument.call_explicit_argument_offsets ==
                    std::vector<std::size_t>{0U} &&
                conservative_argument.call_arguments[0][0].size() == 1 &&
                call_protocol_pta.pointsTo(
                    conservative_argument.call_arguments[0][0][0]).count(
                        call_protocol_pta.unknownObject()) == 1,
            "semantic PTA preserves an empty merged argument slot as explicit unknown data");

    SemanticProgram callee_program{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::Pop},
        {4, SemanticOpcode::LoadLocal, 0, "argument"},
        {6, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis interprocedural;
    const auto callee_cfg = cpygraph::cfg::CFGBuilder().build(std::move(callee_program));
    const auto callee = cpygraph::SemanticPTAConstraintBuilder(interprocedural).build(
        callee_cfg, 2, {"argument"});
    SemanticProgram caller_program{
        {0, SemanticOpcode::LoadGlobal, 0, "callee"},
        {2, SemanticOpcode::LoadGlobal, 0, "condition"},
        {4, SemanticOpcode::ConditionalBranch, 0, "", 10},
        {6, SemanticOpcode::LoadConst, 0},
        {8, SemanticOpcode::Branch, 0, "", 12},
        {10, SemanticOpcode::LoadConst, 1},
        {12, SemanticOpcode::Call, 1},
        {14, SemanticOpcode::Return},
    };
    const auto caller_cfg = cpygraph::cfg::CFGBuilder().build(std::move(caller_program));
    const auto caller = cpygraph::SemanticPTAConstraintBuilder(interprocedural).build(caller_cfg, 1);
    require(callee.value_nodes[0] != caller.value_nodes[0] &&
            callee.allocated_objects[0] != caller.allocated_objects[3],
            "one builder allocates disjoint value and object identities across code objects");
    const auto explicit_call = std::find(
        caller.call_instruction_indices.begin(),
        caller.call_instruction_indices.end(), 6U);
    require(explicit_call != caller.call_instruction_indices.end(),
            "semantic PTA retains the explicit call among protocol sites");
    const auto explicit_call_index = static_cast<std::size_t>(
        explicit_call - caller.call_instruction_indices.begin());
    require(caller.call_arguments[explicit_call_index].size() == 1 &&
            caller.call_arguments[explicit_call_index][0].size() == 2 &&
            caller.call_results[explicit_call_index] == caller.value_nodes[6],
            "one positional argument retains all reaching definitions without changing arity");
    require(caller.call_sites[explicit_call_index].id == caller.value_nodes[6],
            "call-site identity is globally unique across builder results");
    SemanticProgram second_caller_program{
        {0, SemanticOpcode::LoadGlobal, 0, "callee"},
        {2, SemanticOpcode::Call, 0},
        {4, SemanticOpcode::Return},
    };
    const auto second_caller_cfg =
        cpygraph::cfg::CFGBuilder().build(std::move(second_caller_program));
    const auto second_caller =
        cpygraph::SemanticPTAConstraintBuilder(interprocedural).build(second_caller_cfg, 3);
    require(second_caller.call_sites[0].id != caller.call_sites[0].id,
            "call sites in distinct code objects cannot collide");
    cpygraph::InterproceduralPTAStitcher(interprocedural).stitch(
        {{caller.call_arguments[explicit_call_index], callee.formal_parameters,
          callee.return_values, caller.call_results[explicit_call_index]}});
    interprocedural.solve();
    require(interprocedural.pointsTo(callee.formal_parameters[0]).count(
                caller.allocated_objects[3]) == 1 &&
            interprocedural.pointsTo(callee.formal_parameters[0]).count(
                caller.allocated_objects[5]) == 1,
            "interprocedural PTA propagates every branch alternative to one formal parameter");
    require(interprocedural.pointsTo(caller.call_results[explicit_call_index]).count(
                caller.allocated_objects[3]) == 1 &&
            interprocedural.pointsTo(caller.call_results[explicit_call_index]).count(
                caller.allocated_objects[5]) == 1,
            "interprocedural PTA propagates all callee return alternatives to the call result");

    bool rejected_duplicate_parameters = false;
    try {
        cpygraph::SemanticPTAConstraintBuilder(interprocedural).build(
            callee_cfg, 4, {"argument", "argument"});
    } catch (const std::invalid_argument&) { rejected_duplicate_parameters = true; }
    require(rejected_duplicate_parameters,
            "semantic PTA rejects duplicate formal names instead of losing one formal binding");

    bool rejected_return_underflow = false;
    try {
        const auto invalid_return = cpygraph::cfg::CFGBuilder().build(
            {{0, SemanticOpcode::Return}});
        cpygraph::SemanticPTAConstraintBuilder(interprocedural).build(invalid_return, 5);
    } catch (const std::runtime_error&) { rejected_return_underflow = true; }
    require(rejected_return_underflow,
            "semantic PTA rejects explicit returns without an operand");

    bool rejected_invalid_peek = false;
    try {
        SemanticInstruction invalid{2, SemanticOpcode::Generic};
        invalid.stack_peek_count = 2;
        const auto invalid_peek = cpygraph::cfg::CFGBuilder().build(
            {{0, SemanticOpcode::LoadConst, 0}, invalid, {4, SemanticOpcode::Return}});
        cpygraph::SemanticPTAConstraintBuilder(interprocedural).build(invalid_peek, 6);
    } catch (const std::runtime_error&) { rejected_invalid_peek = true; }
    require(rejected_invalid_peek,
            "semantic PTA rejects an invalid stack peek without mutating the stack");

    bool rejected_empty_actual = false;
    try {
        cpygraph::InterproceduralPTAStitcher(interprocedural).stitch(
            {{{{}}, {callee.formal_parameters[0]}, callee.return_values, caller.call_results[0]}});
    } catch (const std::invalid_argument&) { rejected_empty_actual = true; }
    require(rejected_empty_actual,
            "interprocedural PTA rejects a resolved argument without reaching definitions");

    SemanticInstruction keyword_call{0, SemanticOpcode::Call, 2};
    keyword_call.keyword_arguments = true;
    keyword_call.keyword_names = {"ignored", "payload"};
    const auto keyword_bindings = cpygraph::bindArguments(
        keyword_call, 2, {"payload", "ignored"}, 0, 0, 2, false, false);
    require(keyword_bindings == std::vector<cpygraph::ArgumentBinding>{{0, 1}, {1, 0}},
            "keyword names bind values to the named formal rather than stack order");

    SemanticInstruction variadic_call{0, SemanticOpcode::Call, 3};
    const auto variadic_bindings = cpygraph::bindArguments(
        variadic_call, 3, {"head", "tail"}, 1, 0, 0, true, false);
    require(variadic_bindings == std::vector<cpygraph::ArgumentBinding>(
                {{0, 0}, {1, 1}, {2, 1}}),
            "extra positional values flow to the variadic formal");

    SemanticInstruction positional_only_call{0, SemanticOpcode::Call, 1};
    positional_only_call.keyword_arguments = true;
    positional_only_call.keyword_names = {"payload"};
    const auto positional_only_bindings = cpygraph::bindArguments(
        positional_only_call, 1, {"payload", "kwargs"}, 1, 1, 0, false, true);
    require(positional_only_bindings ==
                std::vector<cpygraph::ArgumentBinding>{{0, 1}},
            "keyword matching excludes positional-only formals and uses **kwargs");

    cpygraph::ModuleObjectIndex candidates;
    candidates.suffix.emplace("helper",
        std::vector<cpygraph::ObjectId>{801, 802});
    SemanticProgram ambiguous_import{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::LoadConst, 1},
        {4, SemanticOpcode::ImportModule, 0, "helper", std::nullopt, false, 2},
        {6, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis ambiguous_import_pta;
    const auto ambiguous = cpygraph::SemanticPTAConstraintBuilder(
        ambiguous_import_pta, &candidates).build(
            cpygraph::cfg::CFGBuilder().build(std::move(ambiguous_import)), 12);
    ambiguous_import_pta.solve();
    require(ambiguous_import_pta.pointsTo(ambiguous.value_nodes[2]).count(801) == 1 &&
            ambiguous_import_pta.pointsTo(ambiguous.value_nodes[2]).count(802) == 1,
            "ambiguous relative imports retain every possible module identity");

    cpygraph::ModuleObjectIndex invalid_candidates;
    invalid_candidates.exact.emplace("helper",
        std::vector<cpygraph::ObjectId>{901});
    SemanticInstruction invalid_relative{4, SemanticOpcode::ImportModule, 0, "helper"};
    invalid_relative.discarded_stack_values = 2;
    invalid_relative.import_level = 3;
    SemanticProgram invalid_relative_import{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::LoadConst, 1},
        invalid_relative,
        {6, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis invalid_relative_pta;
    const auto invalid_relative_result = cpygraph::SemanticPTAConstraintBuilder(
        invalid_relative_pta, &invalid_candidates, "pkg.module", false).build(
            cpygraph::cfg::CFGBuilder().build(std::move(invalid_relative_import)), 13);
    invalid_relative_pta.solve();
    require(invalid_relative_pta.pointsTo(
                invalid_relative_result.value_nodes[2]).count(901) == 0,
            "relative imports above the package root cannot alias a top-level module");
    require(invalid_relative_result.import_requests.size() == 1U &&
                invalid_relative_result.import_requests.front().module.empty() &&
                !invalid_relative_result.import_requests.front().relative_name_valid &&
                invalid_relative_result.import_requests.front().lexical_module ==
                    "helper" &&
                invalid_relative_result.import_requests.front().relative_level == 3U,
            "semantic PTA marks invalid relative import requests explicitly");

    SemanticProgram unreachable_import{
        {0, SemanticOpcode::LoadConst, 0},
        {2, SemanticOpcode::Branch, 0, "", 10},
        {4, SemanticOpcode::LoadConst, 0},
        {6, SemanticOpcode::LoadConst, 1},
        {8, SemanticOpcode::ImportModule, 0, "unreachable", std::nullopt,
         false, 2},
        {10, SemanticOpcode::Return},
    };
    cpygraph::AndersenPointerAnalysis unreachable_import_pta;
    const auto unreachable_import_result =
        cpygraph::SemanticPTAConstraintBuilder(unreachable_import_pta).build(
            cpygraph::cfg::CFGBuilder().build(
                std::move(unreachable_import)),
            904);
    require(unreachable_import_result.import_requests.empty(),
            "semantic PTA omits imports in unreachable CFG blocks");
}
