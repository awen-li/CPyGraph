#include "analysis/ddg/interprocedural.h"

#include <stdexcept>

namespace cpygraph::ddg {

DataDependencyGraph InterproceduralStitcher::stitch(
    DataDependencyGraph graph, const std::vector<ResolvedCallFlow>& calls) const {
    for (const auto& call : calls) {
        graph.node(call.call);
        if (call.actual_arguments.size() != call.formal_arguments.size())
            throw std::invalid_argument("resolved call has mismatched actual and formal argument counts");
        for (std::size_t i = 0; i < call.actual_arguments.size(); ++i) {
            graph.node(call.formal_arguments[i]);
            if (call.actual_arguments[i].empty())
                throw std::invalid_argument("resolved DDG argument has no reaching definition");
            for (const auto actual : call.actual_arguments[i]) {
                graph.node(actual);
                graph.addEdge(actual, call.formal_arguments[i], EdgeKind::DefUse);
            }
        }
        for (const auto returned : call.return_values) {
            graph.node(returned);
            graph.addEdge(returned, call.call, EdgeKind::Result);
        }
    }
    return graph;
}

}  // namespace cpygraph::ddg
