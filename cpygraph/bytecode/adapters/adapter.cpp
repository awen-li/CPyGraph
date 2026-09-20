#include "bytecode/adapters/adapter.h"

#include <stdexcept>
#include <limits>

namespace cpygraph::bytecode {

SemanticProgram CPythonAdapter::lift(const std::vector<std::uint8_t>& bytes,
                                     const CodeMetadata& metadata) const {
    SemanticProgram result;
    bool keyword_call_pending = false;
    std::vector<std::string> pending_keyword_names;
    for (const auto& instruction : parser().parse(bytes)) {
        auto semantic = lift(instruction, metadata);
        if (semantic.opcode == SemanticOpcode::PrepareKeywordCall) {
            keyword_call_pending = true;
            if (semantic.operand < metadata.constant_string_tuples.size())
                pending_keyword_names = metadata.constant_string_tuples[semantic.operand];
        } else if (semantic.opcode == SemanticOpcode::Call) {
            semantic.keyword_arguments = semantic.keyword_arguments || keyword_call_pending;
            if (keyword_call_pending) semantic.keyword_names = pending_keyword_names;
            if (semantic.keyword_arguments && semantic.keyword_names.empty() &&
                semantic.discarded_stack_values != 0U && !result.empty() &&
                result.back().opcode == SemanticOpcode::LoadConst &&
                result.back().operand < metadata.constant_string_tuples.size())
                semantic.keyword_names =
                    metadata.constant_string_tuples[result.back().operand];
            keyword_call_pending = false;
            pending_keyword_names.clear();
        }
        if (semantic.opcode == SemanticOpcode::ImportModule && result.size() >= 2U) {
            const auto level = result[result.size() - 2U].constant_integer;
            if (level && *level >= 0 &&
                static_cast<std::uint64_t>(*level) <=
                    std::numeric_limits<std::uint32_t>::max())
                semantic.import_level = static_cast<std::uint32_t>(*level);
            const auto& fromlist = result.back();
            if (fromlist.opcode == SemanticOpcode::LoadConst &&
                fromlist.operand < metadata.constant_string_tuples.size()) {
                semantic.import_from_names =
                    metadata.constant_string_tuples[fromlist.operand];
                semantic.import_fromlist = !semantic.import_from_names.empty();
            }
        }
        result.push_back(std::move(semantic));
    }
    return result;
}

std::vector<ExceptionRegion> CPythonAdapter::exceptionRegions(
    const std::vector<std::uint8_t>&, const CodeMetadata&) const {
    return {};
}

std::vector<ExceptionRegion> decodeExceptionTable(
    const std::vector<std::uint8_t>& encoded) {
    std::vector<ExceptionRegion> regions;
    std::size_t cursor = 0;
    const auto varint = [&]() -> std::uint32_t {
        if (cursor >= encoded.size())
            throw std::invalid_argument("truncated CPython exception table");
        std::uint8_t byte = encoded[cursor++];
        std::uint32_t value = byte & 0x3fU;
        while ((byte & 0x40U) != 0U) {
            if (cursor >= encoded.size())
                throw std::invalid_argument("truncated CPython exception-table varint");
            if (value > 0x03ffffffU)
                throw std::overflow_error("CPython exception-table varint exceeds 32 bits");
            byte = encoded[cursor++];
            value = (value << 6U) | (byte & 0x3fU);
        }
        return value;
    };
    while (cursor < encoded.size()) {
        const auto start_units = varint();
        const auto length_units = varint();
        const auto target_units = varint();
        const auto depth_and_lasti = varint();
        if (length_units == 0)
            throw std::invalid_argument("CPython exception-table region is empty");
        constexpr std::uint32_t code_unit = 2;
        constexpr auto maximum_units = std::numeric_limits<std::uint32_t>::max() /
                                       code_unit;
        if (start_units > maximum_units || length_units > maximum_units ||
            target_units > maximum_units)
            throw std::overflow_error("CPython exception-table offset exceeds 32 bits");
        const auto start = start_units * code_unit;
        const auto length = length_units * code_unit;
        const auto target = target_units * code_unit;
        if (start > std::numeric_limits<std::uint32_t>::max() - length)
            throw std::overflow_error("CPython exception-table range exceeds bytecode offsets");
        regions.push_back(ExceptionRegion{
            start, start + length, target, depth_and_lasti >> 1U,
            1U + (depth_and_lasti & 1U), (depth_and_lasti & 1U) != 0U});
    }
    return regions;
}

}  // namespace cpygraph::bytecode
