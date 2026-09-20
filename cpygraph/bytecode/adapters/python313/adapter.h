#pragma once

#include "bytecode/adapters/table_adapter.h"

namespace cpygraph::bytecode {

class Python313Adapter final : public TableDrivenCPythonAdapter {
public:
    using TableDrivenCPythonAdapter::lift;
    Python313Adapter();
    std::string_view version() const noexcept override { return "3.13"; }
};

}  // namespace cpygraph::bytecode
