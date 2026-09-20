#pragma once

#include "analysis/cg/call_graph.h"
#include "analysis/argument_binding.h"
#include "analysis/pta/sensitivity.h"
#include "package/analysis.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cpygraph::package {

// Compact lattice value for relations that the concrete graph cannot yet
// enumerate. TypedUnresolved means that unresolved relations are limited by
// their semantic source/kind. ConservativeTop denotes every relation in the
// consumer's domain and must only be used when no narrower classification is
// available.
enum class SoundnessCoverage : std::uint8_t {
    ConcreteOnly,
    ConservativeTop,
    TypedUnresolved,
};

struct PackageCallGraphResult {
    cg::CallGraph graph;
    std::size_t analyzed_code_object_count{};
    std::size_t activated_callable_count{};
    std::size_t call_site_count{};
    SoundnessCoverage points_to_coverage{SoundnessCoverage::ConservativeTop};
    SoundnessCoverage call_graph_coverage{SoundnessCoverage::ConservativeTop};
    struct PTAStatistics {
        std::size_t value_count{};
        std::size_t object_count{};
        std::size_t constraint_count{};
        std::size_t points_to_fact_count{};
        std::size_t content_fact_count{};
        std::size_t field_fact_count{};
        std::size_t solver_iteration_count{};
    } points_to_statistics;
};

// Optional numeric snapshot for clients that need to correlate package-level
// PTA/CG results with semantic instructions. It is populated only on request;
// normal graph construction pays no retained-memory cost and graph nodes and
// edges remain string-free.
struct PTAValueObservation {
    CodeObjectId code{};
    std::size_t instruction_index{};
    NodeId value{};
    std::vector<ObjectId> objects;
    PTAContextId context{};
    std::uint8_t output_index{};
};

enum class PTAObjectOriginKind : std::uint8_t {
    Instruction,
    Parameter,
    ClassInstance,
    Module,
    Callable,
};

struct PTAObjectOrigin {
    ObjectId object{};
    PTAObjectOriginKind kind{PTAObjectOriginKind::Instruction};
    CodeObjectId code{};
    std::size_t index{};
    PTAContextId context{};
};

struct PackageCallSiteObservation {
    std::uint64_t site{};
    CodeObjectId code{};
    std::size_t instruction_index{};
    PTAContextId context{};
    NodeId result_value{};
    // Normalized argument values preserve all reaching PTA alternatives.
    // explicit_argument_offset excludes an implicit method receiver when
    // matching zero-based source-level call arguments.
    std::vector<std::vector<NodeId>> arguments;
    std::size_t explicit_argument_offset{};
    // Numeric value consumed as the callable operand at this site.
    NodeId callee_value{};
    // Pre-aggregation stack values that flow into callee_value. This retains
    // the producing attribute load across nested argument evaluation.
    std::vector<NodeId> callee_inputs;
};

// An exact module-qualified external callable observed at a call site. The
// graph remains numeric; presentation/model clients opt into these strings by
// requesting the package observation snapshot.
struct PackageExternalCallObservation {
    std::uint64_t site{};
    CodeObjectId code{};
    std::size_t instruction_index{};
    PTAContextId context{};
    std::string qualified_target;
};

// Stable association created when a function object is materialized. The
// defining instruction is kept separately from the callable body so clients
// can distinguish multiple runtime objects for one lexical code object.
struct PackageCallableObjectObservation {
    ObjectId object{};
    FunctionId function{};
    CodeObjectId code{};
    CodeObjectId defining_code{};
    std::size_t instruction_index{};
    PTAContextId defining_context{};
};

enum class PTAAttributeAccessKind : std::uint8_t {
    Load,
    Store,
};

// Instruction-specific operand roles for an attribute access. Field names
// remain outside the record and are available through attribute_field_names.
// A single role may retain several reaching value IDs.
struct PTAAttributeAccessObservation {
    CodeObjectId code{};
    std::size_t instruction_index{};
    PTAContextId context{};
    PTAAttributeAccessKind kind{PTAAttributeAccessKind::Load};
    FieldId field{};
    std::vector<NodeId> base_values;
    std::vector<NodeId> values;
};

// Instruction-specific operand roles for an item load. The record exposes
// existing numeric PTA values only; observing it does not add graph facts or
// change protocol dispatch.
struct PTAElementLoadObservation {
    CodeObjectId code{};
    std::size_t instruction_index{};
    PTAContextId context{};
    std::vector<NodeId> base_values;
    std::vector<NodeId> key_values;
    std::vector<NodeId> values;
};

struct PackageCallActivationObservation {
    std::uint64_t site{};
    CodeObjectId caller{};
    std::size_t instruction_index{};
    PTAContextId caller_context{};
    CodeObjectId target{};
    // Context of the activated target body.
    PTAContextId context{};
    std::vector<ArgumentBinding> bindings;
};

// A code body that Python executes eagerly as part of evaluating its defining
// code object. The instruction index refers to the source's semantic program;
// consumers can recover an exact bytecode offset without parsing raw opcodes.
struct PackageEagerCodeExecutionObservation {
    CodeObjectId source{};
    std::size_t instruction_index{};
    PTAContextId source_context{};
    CodeObjectId target{};
    PTAContextId target_context{};
};

// A version-independent Python import request. The module name is absolute
// after resolving relative-import syntax against its defining module.
struct PackageImportRequestObservation {
    CodeObjectId source{};
    std::size_t instruction_index{};
    PTAContextId source_context{};
    std::string module;
    std::vector<std::string> from_names;
    bool relative_name_valid{true};
    // The lexical name and level are retained when an invalid relative
    // request cannot have an absolute module identity.
    std::string lexical_module;
    std::uint32_t relative_level{};
};

struct PTAContentObservation {
    ObjectId base{};
    ObjectId object{};
};

struct PTAFieldObservation {
    ObjectId base{};
    FieldId field{};
    ObjectId object{};
};

struct PTAFieldNameObservation {
    FieldId field{};
    std::string name;
};

// Exact Python module identity for a PTA object. This keeps module names out
// of the graph while allowing clients to qualify receiver-sensitive facts.
struct PTAModuleObjectObservation {
    ObjectId object{};
    std::string name;
};

// Exact PTA receiver objects for a heap access. This remains separate from
// the instruction's primary value because newer CPython releases can fuse two
// local loads into one bytecode instruction while retaining distinct values.
struct PTAHeapBaseObservation {
    CodeObjectId code{};
    std::size_t instruction_index{};
    PTAContextId context{};
    std::vector<ObjectId> objects;
};

struct PackageAnalysisObservations {
    std::vector<PTAValueObservation> points_to;
    std::vector<PTAObjectOrigin> object_origins;
    std::vector<PTAContentObservation> contents;
    std::vector<PTAFieldObservation> fields;
    std::vector<PTAFieldNameObservation> field_names;
    // Includes names observed at attribute accesses even when no concrete heap
    // field fact was materialized for that field.
    std::vector<PTAFieldNameObservation> attribute_field_names;
    std::vector<PTAModuleObjectObservation> module_objects;
    std::vector<PTAHeapBaseObservation> heap_bases;
    std::vector<PTAAttributeAccessObservation> attribute_accesses;
    std::vector<PTAElementLoadObservation> element_loads;
    std::vector<PackageCallableObjectObservation> callable_objects;
    std::vector<PackageCallSiteObservation> call_sites;
    std::vector<PackageExternalCallObservation> external_calls;
    std::vector<PackageCallActivationObservation> call_activations;
    std::vector<PackageEagerCodeExecutionObservation> eager_code_executions;
    std::vector<PackageImportRequestObservation> import_requests;
};

// Optional demand set for exact module-qualified external callable
// observations. Dynamic-import identities are always retained because they
// affect package reachability; other attributes are materialized only when a
// client requests their qualified targets.
struct ExternalCallConfiguration {
    std::vector<std::string> qualified_targets;
};

// Starts from module bodies and constructs PTA constraints for nested code
// objects only when call-graph discovery makes those bodies reachable.
class OnTheFlyCallGraphAnalyzer {
public:
    PackageCallGraphResult analyze(
        const PackageAnalysis& package,
        const std::vector<CodeObjectId>& entry_points = {},
        PackageAnalysisObservations* observations = nullptr,
        const PTASensitivityConfiguration& sensitivity = {},
        const ExternalCallConfiguration& external_calls = {}) const;
};

}  // namespace cpygraph::package
