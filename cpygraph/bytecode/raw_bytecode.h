#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cpygraph::bytecode {

struct RawInstruction {
    std::uint32_t offset{};
    std::uint8_t opcode{};
    std::uint32_t argument{};
    // Number of bytes occupied by EXTENDED_ARG prefixes.  `offset` is the
    // boundary at which CPython may jump; the physical opcode is at
    // `offset + prefix_size`.
    std::uint32_t prefix_size{};
};

class WordcodeParser {
public:
    explicit WordcodeParser(std::uint8_t extended_arg_opcode,
                            std::uint8_t cache_opcode = 0,
                            bool omit_caches = false) noexcept;

    std::vector<RawInstruction> parse(const std::vector<std::uint8_t>& bytes) const;

private:
    std::uint8_t extended_arg_opcode_;
    std::uint8_t cache_opcode_;
    bool omit_caches_;
};

}  // namespace cpygraph::bytecode
