#include "analysis/argument_binding.h"

#include <algorithm>

namespace cpygraph {
namespace {

void addBinding(std::vector<ArgumentBinding>& bindings, std::size_t actual,
                std::size_t formal) {
    const auto binding = ArgumentBinding{actual, formal};
    if (std::find(bindings.begin(), bindings.end(), binding) == bindings.end())
        bindings.push_back(binding);
}

void bindConservatively(std::vector<ArgumentBinding>& bindings,
                        std::size_t actual_count, std::size_t formal_count) {
    for (std::size_t actual = 0; actual < actual_count; ++actual)
        for (std::size_t formal = 0; formal < formal_count; ++formal)
            addBinding(bindings, actual, formal);
}

}  // namespace

std::vector<ArgumentBinding> bindArguments(
    const bytecode::SemanticInstruction& call,
    std::size_t actual_count,
    const std::vector<std::string>& formal_names,
    std::size_t positional_count,
    std::size_t positional_only_count,
    std::size_t keyword_only_count,
    bool has_var_arguments,
    bool has_var_keywords) {
    std::vector<ArgumentBinding> bindings;
    const auto formal_count = formal_names.size();
    if (actual_count == 0 || formal_count == 0) return bindings;
    if (call.expanded_arguments ||
        (call.keyword_arguments && call.keyword_names.empty()) ||
        call.keyword_names.size() > actual_count) {
        bindConservatively(bindings, actual_count, formal_count);
        return bindings;
    }

    positional_count = std::min(positional_count, formal_count);
    positional_only_count = std::min(positional_only_count, positional_count);
    keyword_only_count = std::min(keyword_only_count,
        formal_count - positional_count);
    const auto vararg_index = positional_count + keyword_only_count;
    const auto varkw_index = vararg_index + (has_var_arguments ? 1U : 0U);
    const auto keyword_count = call.keyword_arguments
        ? call.keyword_names.size() : 0U;
    const auto positional_actuals = actual_count - keyword_count;

    for (std::size_t actual = 0; actual < positional_actuals; ++actual) {
        if (actual < positional_count) {
            addBinding(bindings, actual, actual);
        } else if (has_var_arguments && vararg_index < formal_count) {
            addBinding(bindings, actual, vararg_index);
        } else {
            // This can arise from an imprecisely resolved bound call. Preserve
            // possible flow instead of discarding the target.
            for (std::size_t formal = 0; formal < formal_count; ++formal)
                addBinding(bindings, actual, formal);
        }
    }

    const auto named_formal_count = positional_count + keyword_only_count;
    for (std::size_t keyword = 0; keyword < keyword_count; ++keyword) {
        const auto actual = positional_actuals + keyword;
        const auto& name = call.keyword_names[keyword];
        const auto found = std::find(
            formal_names.begin() + static_cast<std::ptrdiff_t>(positional_only_count),
            formal_names.begin() + static_cast<std::ptrdiff_t>(named_formal_count),
            name);
        if (found != formal_names.begin() +
                static_cast<std::ptrdiff_t>(named_formal_count)) {
            addBinding(bindings, actual,
                static_cast<std::size_t>(found - formal_names.begin()));
        } else if (has_var_keywords && varkw_index < formal_count) {
            addBinding(bindings, actual, varkw_index);
        } else {
            bindConservatively(bindings, actual_count, formal_count);
            return bindings;
        }
    }
    return bindings;
}

}  // namespace cpygraph
