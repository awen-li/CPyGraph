#pragma once

#include "common/model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cpygraph {

using PTAContextId = std::uint64_t;

enum class PTASensitivity : std::uint8_t {
    None = 0U,
    Flow = 1U << 0U,
    Context = 1U << 1U,
    Path = 1U << 2U,
};

constexpr PTASensitivity operator|(PTASensitivity first,
                                   PTASensitivity second) noexcept {
    return static_cast<PTASensitivity>(
        static_cast<std::uint8_t>(first) |
        static_cast<std::uint8_t>(second));
}

constexpr PTASensitivity operator&(PTASensitivity first,
                                   PTASensitivity second) noexcept {
    return static_cast<PTASensitivity>(
        static_cast<std::uint8_t>(first) &
        static_cast<std::uint8_t>(second));
}

constexpr bool hasSensitivity(PTASensitivity attributes,
                              PTASensitivity attribute) noexcept {
    return (attributes & attribute) != PTASensitivity::None;
}

struct PTAFunctionSensitivity {
    CodeObjectId function{};
    PTASensitivity attributes{PTASensitivity::None};
};

enum class PTASensitivityLevel : std::uint8_t {
    Insensitive,
    Selective,
    Complete,
};

inline constexpr std::size_t kDefaultMaximumPTAPathVariants = 16U;
inline constexpr PTASensitivity kAllPTASensitivities =
    PTASensitivity::Flow | PTASensitivity::Context | PTASensitivity::Path;

// Numeric function-level configuration. Unlisted functions use the default
// flow-, context-, and path-insensitive PTA. Path partitions are bounded to
// keep selective precision practical; unpartitioned branches remain soundly
// merged when this limit is reached.
struct PTASensitivityConfiguration {
    PTASensitivityLevel level{PTASensitivityLevel::Insensitive};
    std::vector<PTAFunctionSensitivity> functions;
    std::size_t maximum_path_variants{kDefaultMaximumPTAPathVariants};
};

}  // namespace cpygraph
