#pragma once

#include "analysis/cfg/control_flow_graph.h"
#include "analysis/ddg/data_dependency_graph.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cpygraph::ddg {

struct DDGCallBoundary {
    std::size_t instruction_index{};
    NodeId call{};
    std::vector<std::vector<NodeId>> actual_arguments;
};

struct DDGCollectionBoundary {
    std::size_t instruction_index{};
    NodeId collection{};
    // Reaching definitions in source stack order. Maps therefore preserve
    // key/value alternation, while tuples and lists preserve element order.
    std::vector<std::vector<NodeId>> elements;
};

enum class DDGAccessKind : std::uint8_t { Load, Store };
enum class DDGHeapLocationKind : std::uint8_t { Attribute, Element };

struct DDGHeapAccess {
    std::size_t instruction_index{};
    cfg::BlockId block{};
    NodeId node{};
    DDGAccessKind kind{DDGAccessKind::Load};
    DDGHeapLocationKind location{DDGHeapLocationKind::Attribute};
    std::string field;
    std::vector<NodeId> bases;
    // Subscript element accesses preserve explicit key definitions separately
    // from receivers. Collection-building stores and attributes leave it empty.
    std::vector<NodeId> keys;
};

struct DDGGlobalAccess {
    NodeId node{};
    DDGAccessKind kind{DDGAccessKind::Load};
    std::string name;
};

struct DDGBuildResult {
    DataDependencyGraph graph;
    std::vector<NodeId> formal_parameters;
    std::vector<NodeId> return_values;
    std::vector<DDGCallBoundary> call_boundaries;
    std::vector<DDGCollectionBoundary> collection_boundaries;
    std::vector<DDGHeapAccess> heap_accesses;
    std::vector<DDGGlobalAccess> global_accesses;
};

class DDGBuilder {
public:
    DataDependencyGraph build(const cfg::ControlFlowGraph& cfg,
                              CodeObjectId code = 0) const;
    DDGBuildResult buildWithBoundaries(
        const cfg::ControlFlowGraph& cfg,
        CodeObjectId code,
        const std::vector<std::string>& parameter_names) const;
};

}  // namespace cpygraph::ddg
