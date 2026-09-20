#include "analysis/pta/interprocedural_pta.h"

#include <stdexcept>

namespace cpygraph {

void InterproceduralPTAStitcher::stitch(const std::vector<ResolvedPTACallFlow>& calls) {
    for (const auto& call : calls) {
        if (call.call_result == 0)
            throw std::invalid_argument("PTA call result id zero is reserved");
        auto bindings = call.argument_bindings;
        if (bindings.empty()) {
            const auto count = std::min(call.actual_arguments.size(),
                                        call.formal_parameters.size());
            for (std::size_t index = 0; index < count; ++index)
                bindings.emplace_back(index, index);
        }
        for (const auto& [actual_index, formal_index] : bindings) {
            if (actual_index >= call.actual_arguments.size() ||
                formal_index >= call.formal_parameters.size())
                throw std::invalid_argument("PTA argument binding index is out of range");
            if (call.formal_parameters[formal_index] == 0 ||
                call.actual_arguments[actual_index].empty())
                throw std::invalid_argument("PTA resolved arguments require nonzero reaching values");
            for (const auto actual : call.actual_arguments[actual_index])
                analysis_.addCopy(actual, call.formal_parameters[formal_index]);
        }
        for (const auto returned : call.return_values)
            analysis_.addCopy(returned, call.call_result);
    }
}

}  // namespace cpygraph
