#pragma once

#include "bytecode/adapters/table_adapter.h"

namespace cpygraph::bytecode {

class Python310Adapter final : public TableDrivenCPythonAdapter {
public:
    using TableDrivenCPythonAdapter::lift;
    Python310Adapter();
    std::string_view version() const noexcept override { return "3.10"; }
    SemanticInstruction lift(const RawInstruction& instruction,
                             const CodeMetadata& metadata) const override;
    std::vector<ExceptionRegion> exceptionRegions(
        const std::vector<std::uint8_t>& bytes,
        const CodeMetadata& metadata) const override;
};

}  // namespace cpygraph::bytecode
