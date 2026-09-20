#pragma once

#include "analysis/argument_binding.h"
#include "analysis/cg/call_graph.h"

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cpygraph::cg {

struct OnTheFlyCallSite {
    CallSite site;
    std::vector<std::vector<NodeId>> actual_arguments;
    NodeId call_result{};
    // Opaque instruction position retained for target-specific argument binding.
    std::size_t instruction_index{};
    std::uint64_t caller_context{};
};

struct CallableBodySummary {
    std::vector<NodeId> formal_parameters;
    std::vector<NodeId> return_values;
    std::vector<OnTheFlyCallSite> call_sites;
    // Hidden numeric binding for the concrete function object whose capture
    // fields supply this independently analyzed code object's free cells.
    NodeId capture_environment{};
};

struct OnTheFlyCallGraphResult {
    CallGraph graph;
    std::size_t call_site_count{};
    std::size_t activated_callable_count{};
};

// Couples incremental points-to propagation with call-graph discovery.
// BodyProvider adds a newly reachable callable's local constraints and returns
// its interprocedural boundary. BindingProvider may provide target-specific
// argument bindings; an empty result uses positional binding.
class OnTheFlyCallGraphBuilder {
public:
    using ContextId = std::uint64_t;
    using BodyProvider = std::function<CallableBodySummary(CallableTarget)>;
    using ContextualBodyProvider = std::function<CallableBodySummary(
        CallableTarget, ContextId)>;
    using ContextSelector = std::function<ContextId(
        const OnTheFlyCallSite&, CallableTarget)>;
    using BindingProvider = std::function<std::vector<ArgumentBinding>(
        const OnTheFlyCallSite&, CallableTarget)>;
    using UnresolvedSiteHandler = std::function<bool(
        const std::vector<std::uint64_t>&)>;

    explicit OnTheFlyCallGraphBuilder(PointerAnalysis& pointer_analysis) noexcept;

    void registerCallable(ObjectId object, CallableTarget target);
    // Models an explicit language-level overwrite of an existing callable
    // binding. Unlike registration, assignment intentionally replaces the
    // previous target after validating both identifiers.
    void assignCallable(ObjectId object, CallableTarget target);
    // Seeds an externally selected analysis root as already activated. Calls
    // back into this target reuse the body instead of registering its sites twice.
    void addEntryPoint(CallableTarget target, CallableBodySummary body);
    void addCallSite(OnTheFlyCallSite site);
    void addCallSites(std::vector<OnTheFlyCallSite> sites);
    // PTA may refine an implicit Python dispatch after receiver fields reach
    // the fixed point. Update the already-registered site without rebuilding
    // or duplicating it.
    void updateUnresolvedGroup(std::uint64_t site,
                               UnresolvedCalleeGroup group);
    OnTheFlyCallGraphResult build(const BodyProvider& body_provider,
                                  const BindingProvider& binding_provider = {});
    OnTheFlyCallGraphResult build(
        const ContextualBodyProvider& body_provider,
        const ContextSelector& context_selector,
        const BindingProvider& binding_provider = {},
        const UnresolvedSiteHandler& unresolved_site_handler = {});

private:
    struct TargetKey {
        FunctionId function{};
        CodeObjectId code{};
        bool operator==(const TargetKey& other) const noexcept {
            return function == other.function && code == other.code;
        }
    };
    struct TargetKeyHash {
        std::size_t operator()(const TargetKey& target) const noexcept;
    };
    struct BodyKey {
        TargetKey target;
        ContextId context{};
        bool operator==(const BodyKey& other) const noexcept {
            return target == other.target && context == other.context;
        }
    };
    struct BodyKeyHash {
        std::size_t operator()(const BodyKey& body) const noexcept;
    };
    struct EdgeKey {
        std::uint64_t site{};
        TargetKey target;
        bool operator==(const EdgeKey& other) const noexcept {
            return site == other.site && target == other.target;
        }
    };
    struct EdgeKeyHash {
        std::size_t operator()(const EdgeKey& edge) const noexcept;
    };

    const CallableBodySummary& activate(CallableTarget target, ContextId context,
                                        const ContextualBodyProvider& provider,
                                        bool& changed);
    void validateSite(const OnTheFlyCallSite& site) const;
    void validateSites(const std::vector<OnTheFlyCallSite>& sites) const;

    PointerAnalysis& pointer_analysis_;
    std::unordered_map<ObjectId, CallableTarget> callables_;
    std::vector<OnTheFlyCallSite> call_sites_;
    std::unordered_set<std::uint64_t> call_site_ids_;
    std::unordered_map<BodyKey, CallableBodySummary, BodyKeyHash> bodies_;
    std::unordered_map<NodeId, std::unordered_set<ObjectId>> capture_objects_;
};

}  // namespace cpygraph::cg
