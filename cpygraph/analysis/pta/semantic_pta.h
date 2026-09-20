#pragma once

#include "analysis/cfg/control_flow_graph.h"
#include "analysis/cg/call_graph.h"
#include "analysis/pta/pointer_analysis.h"

#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cpygraph {

inline constexpr std::string_view kPTAClassInstanceFieldName =
    "$cpygraph.class.instance";
inline constexpr std::string_view kPTACellContentsFieldName =
    "$cpygraph.cell.contents";
inline constexpr std::string_view kPTACaptureFieldPrefix =
    "$cpygraph.capture.";
inline constexpr std::string_view kPTAFunctionAttributeFieldName =
    "__function_attribute__";

struct ModuleObjectIndex {
    std::unordered_map<std::string, std::vector<ObjectId>> exact;
    std::unordered_map<std::string, std::vector<ObjectId>> suffix;
};

struct PTAProtocolReceiverSet {
    std::size_t instruction_index{};
    std::vector<NodeId> primary;
    std::vector<NodeId> secondary;
};

struct PTAImportRequest {
    std::size_t instruction_index{};
    std::string module;
    std::vector<std::string> from_names;
    bool relative_name_valid{true};
    std::string lexical_module;
    std::uint32_t relative_level{};
};

struct PTAConstraintResult {
    std::vector<NodeId> value_nodes;
    // Fused CPython local operations can produce two logically independent
    // values from one bytecode instruction. Keep the second value separate so
    // PTA does not merge unrelated locals merely because one release fused
    // their loads or stores.
    std::vector<NodeId> secondary_value_nodes;
    std::vector<ObjectId> allocated_objects;
    std::vector<cg::CallSite> call_sites;
    std::vector<std::size_t> call_instruction_indices;
    std::vector<NodeId> formal_parameters;
    std::vector<NodeId> return_values;
    // call -> positional argument -> alternative reaching value nodes
    std::vector<std::vector<std::vector<NodeId>>> call_arguments;
    // Number of leading normalized arguments supplied by the call protocol
    // rather than written explicitly at the source call site.
    std::vector<std::size_t> call_explicit_argument_offsets;
    // Exact stack values consumed as the callable operand before they are
    // copied into the call site's aggregate callee node.
    std::vector<std::vector<NodeId>> call_callee_inputs;
    std::vector<NodeId> call_results;
    std::unordered_map<std::string, std::vector<NodeId>> global_inputs;
    std::unordered_map<std::string, std::vector<NodeId>> global_definitions;
    std::unordered_map<std::string, std::vector<NodeId>> local_inputs;
    std::unordered_map<std::string, NodeId> lexical_cell_bindings;
    std::vector<NodeId> star_import_sources;
    // Import requests retain the fully resolved relative module name and the
    // decoded fromlist independently of CPython opcode layout.
    std::vector<PTAImportRequest> import_requests;
    // Indexed by semantic instruction. Attribute receivers are retained so
    // semantic identities discovered after the PTA fixed point can be linked
    // to the corresponding attribute result.
    std::vector<std::vector<NodeId>> attribute_load_bases;
    // Indexed by semantic instruction. These preserve the two operand roles
    // of STORE_ATTR so clients can recover typed identity edges after the
    // points-to fixed point resolves aliases.
    std::vector<std::vector<NodeId>> attribute_store_bases;
    std::vector<std::vector<NodeId>> attribute_store_values;
    // Indexed by semantic instruction. Preserve the receiver and key roles of
    // item loads so package clients can identify mapping-value boundaries
    // without reconstructing the abstract operand stack.
    std::vector<std::vector<NodeId>> element_load_bases;
    std::vector<std::vector<NodeId>> element_load_keys;
    // Numeric receiver values for deferred Python protocol selection.  The
    // package coordinator resolves ordered fallback only after PTA has found
    // receiver objects and their available special methods.
    std::vector<PTAProtocolReceiverSet> protocol_receivers;
};

struct PTAPathDecision {
    std::size_t instruction_index{};
    bool take_jump{false};
};

struct SemanticPTAOptions {
    bool flow_sensitive{false};
    // Module bodies execute once in bytecode order. Preserve strong updates
    // for their namespace independently of optional function sensitivity.
    bool ordered_module_globals{false};
    bool allocation_sensitive{false};
    bool defer_global_unknowns{false};
    bool defer_call_fallbacks{false};
    bool seed_external_parameters{true};
    std::vector<PTAPathDecision> path_decisions;
};

// Converts version-independent semantic bytecode into inclusion constraints.
class SemanticPTAConstraintBuilder {
public:
    explicit SemanticPTAConstraintBuilder(
        PointerAnalysis& analysis,
        const ModuleObjectIndex* module_objects = nullptr,
        std::string module_name = {}, bool module_is_package = false,
        ObjectId receiver_object = 0,
        SemanticPTAOptions options = {}) noexcept
        : analysis_(analysis), module_objects_(module_objects),
          module_name_(std::move(module_name)), module_is_package_(module_is_package),
          receiver_object_(receiver_object), options_(std::move(options)) {}

    PTAConstraintResult build(const cfg::ControlFlowGraph& cfg, CodeObjectId caller,
                              const std::vector<std::string>& parameters = {},
                              const std::vector<std::string>& free_names = {});

private:
    PointerAnalysis& analysis_;
    const ModuleObjectIndex* module_objects_;
    std::string module_name_;
    bool module_is_package_;
    ObjectId receiver_object_{};
    SemanticPTAOptions options_;
};

}  // namespace cpygraph
