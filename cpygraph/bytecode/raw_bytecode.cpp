#include "bytecode/raw_bytecode.h"

#include <stdexcept>

namespace cpygraph::bytecode {

WordcodeParser::WordcodeParser(std::uint8_t extended_arg_opcode,
                               std::uint8_t cache_opcode,
                               bool omit_caches) noexcept
    : extended_arg_opcode_(extended_arg_opcode),
      cache_opcode_(cache_opcode),
      omit_caches_(omit_caches) {}

std::vector<RawInstruction> WordcodeParser::parse(const std::vector<std::uint8_t>& bytes) const {
    if (bytes.size() % 2 != 0) throw std::invalid_argument("wordcode must contain complete two-byte code units");
    std::vector<RawInstruction> result;
    std::uint32_t extended = 0;
    std::uint32_t prefix_size = 0;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2) {
        const auto opcode = bytes[offset];
        const auto argument = extended | bytes[offset + 1];
        if (opcode == extended_arg_opcode_) {
            if (extended > 0x00ffffffU) throw std::overflow_error("EXTENDED_ARG exceeds 32-bit argument width");
            extended = argument << 8U;
            prefix_size += 2U;
            continue;
        }
        if (!(omit_caches_ && opcode == cache_opcode_)) {
            result.push_back({static_cast<std::uint32_t>(offset) - prefix_size,
                              opcode, argument, prefix_size});
        }
        extended = 0;
        prefix_size = 0;
    }
    if (prefix_size != 0) throw std::invalid_argument("dangling EXTENDED_ARG at end of bytecode");
    return result;
}

}  // namespace cpygraph::bytecode
