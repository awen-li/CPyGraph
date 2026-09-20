#pragma once

#include "bytecode/adapters/table_adapter.h"

namespace cpygraph::bytecode {

class Python314Adapter final : public TableDrivenCPythonAdapter {
public:
    using TableDrivenCPythonAdapter::lift;
    Python314Adapter();
    std::string_view version() const noexcept override { return "3.14"; }
    SemanticInstruction lift(const RawInstruction& instruction,
                             const CodeMetadata& metadata) const override;
};

}  // namespace cpygraph::bytecode
