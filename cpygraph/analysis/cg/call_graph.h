#pragma once

#include "common/model.h"
#include "analysis/pta/pointer_analysis.h"
#include "bytecode/python_protocol.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace cpygraph::cg {

struct CallableTarget {
    FunctionId function{};
    CodeObjectId code{};
};

// Compact optional representation. Callable target identifiers reserve zero,
// so no discriminator byte or std::optional padding is required per edge.
class OptionalCallableTarget {
public:
    constexpr OptionalCallableTarget() noexcept = default;
    constexpr OptionalCallableTarget(std::nullopt_t) noexcept {}
    constexpr OptionalCallableTarget(CallableTarget target) noexcept
        : target_(target) {}

    constexpr bool has_value() const noexcept { return target_.code != 0U; }
    constexpr explicit operator bool() const noexcept { return has_value(); }
    constexpr const CallableTarget& operator*() const noexcept { return target_; }
    constexpr const CallableTarget* operator->() const noexcept { return &target_; }

private:
    CallableTarget target_{};
};

enum class UnresolvedCalleeKind : std::uint8_t {
    DynamicCall,
    PythonProtocol,
};

// Describes the runtime target set retained when PTA cannot enumerate every
// callable. candidate_methods preserve semantic information such as __exit__
// or the __add__/__radd__ pair instead of collapsing the edge to "unknown".
struct UnresolvedCalleeGroup {
    constexpr UnresolvedCalleeGroup() noexcept = default;
    constexpr UnresolvedCalleeGroup(
        UnresolvedCalleeKind selected_kind,
        bytecode::PythonProtocolOperation selected_operation,
        bytecode::PythonMethodSet selected_methods,
        bytecode::PythonExternalCallee selected_external =
            bytecode::PythonExternalCallee::None) noexcept
        : kind(selected_kind), operation(selected_operation),
          external_callee(selected_external),
          candidate_methods(selected_methods) {}

    UnresolvedCalleeKind kind{UnresolvedCalleeKind::DynamicCall};
    bytecode::PythonProtocolOperation operation{
        bytecode::PythonProtocolOperation::DynamicCall};
    bytecode::PythonExternalCallee external_callee{
        bytecode::PythonExternalCallee::None};
    bytecode::PythonMethodSet candidate_methods{};

    bool operator==(const UnresolvedCalleeGroup& other) const noexcept {
        return kind == other.kind && operation == other.operation &&
               external_callee == other.external_callee &&
               candidate_methods == other.candidate_methods;
    }
};

using CalleeGroupId = std::uint32_t;
using CallGraphDegree = std::uint32_t;

inline constexpr CallGraphDegree kInitialCallGraphDegree = 0U;

// Compact node metadata maintained from the call-site edges. Unknown targets
// contribute to the caller's out-degree but do not create a synthetic node.
struct CallGraphNode {
    CodeObjectId code{};
    CallGraphDegree in_degree{kInitialCallGraphDegree};
    CallGraphDegree out_degree{kInitialCallGraphDegree};

    std::size_t degree() const noexcept {
        return static_cast<std::size_t>(in_degree) +
               static_cast<std::size_t>(out_degree);
    }
};

struct CallSite {
    std::uint64_t id{};
    CodeObjectId caller{};
    NodeId callee_value{};
    UnresolvedCalleeGroup unresolved_group;
};

struct CallEdge {
    std::uint64_t site{};
    CodeObjectId caller{};
    OptionalCallableTarget target;
    CalleeGroupId unresolved_group{};
};

class CallGraph {
public:
    explicit CallGraph(std::vector<CallEdge> edges,
                       std::vector<UnresolvedCalleeGroup> groups = {});
    const std::vector<CallEdge>& edges() const noexcept { return edges_; }
    const std::vector<CallGraphNode>& nodes() const noexcept { return nodes_; }
    const CallGraphNode* node(CodeObjectId code) const noexcept;
    std::vector<CallEdge> targets(std::uint64_t site) const;
    const UnresolvedCalleeGroup& unresolvedGroup(CalleeGroupId id) const;
    std::size_t unresolvedGroupCount() const noexcept { return groups_.size(); }

private:
    std::vector<CallEdge> edges_;
    std::vector<UnresolvedCalleeGroup> groups_;
    std::vector<CallGraphNode> nodes_;
};

class CallGraphBuilder {
public:
    explicit CallGraphBuilder(PointerAnalysis& pointer_analysis) noexcept;
    void registerCallable(ObjectId object, CallableTarget target);
    CallGraph build(const std::vector<CallSite>& sites);

private:
    PointerAnalysis& pointer_analysis_;
    std::unordered_map<ObjectId, CallableTarget> callables_;
};

}  // namespace cpygraph::cg
