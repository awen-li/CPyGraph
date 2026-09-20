#pragma once

#include "bytecode/raw_bytecode.h"
#include "bytecode/semantic_ir.h"

#include <string_view>
#include <vector>

namespace cpygraph::bytecode {

class CPythonAdapter {
public:
    virtual ~CPythonAdapter() = default;
    virtual std::string_view version() const noexcept = 0;
    virtual WordcodeParser parser() const = 0;
    virtual SemanticInstruction lift(const RawInstruction& instruction,
                                     const CodeMetadata& metadata) const = 0;
    virtual std::vector<ExceptionRegion> exceptionRegions(
        const std::vector<std::uint8_t>& bytes,
        const CodeMetadata& metadata) const;

    SemanticProgram lift(const std::vector<std::uint8_t>& bytes,
                         const CodeMetadata& metadata) const;
};

// CPython 3.11+ uses a compact table whose encoding is private to the
// bytecode adapter layer. Exposed for adapter reuse and focused tests only.
std::vector<ExceptionRegion> decodeExceptionTable(
    const std::vector<std::uint8_t>& encoded);

}  // namespace cpygraph::bytecode
