#pragma once

#include "bytecode/semantic_ir.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace cpygraph {

using ArgumentBinding = std::pair<std::size_t, std::size_t>;

// Returns (actual index, formal index) relationships. Keyword names and
// variadic slots are normalized here so PTA and DDG use identical
// version-independent call semantics.
std::vector<ArgumentBinding> bindArguments(
    const bytecode::SemanticInstruction& call,
    std::size_t actual_count,
    const std::vector<std::string>& formal_names,
    std::size_t positional_count,
    std::size_t positional_only_count,
    std::size_t keyword_only_count,
    bool has_var_arguments,
    bool has_var_keywords);

}  // namespace cpygraph
