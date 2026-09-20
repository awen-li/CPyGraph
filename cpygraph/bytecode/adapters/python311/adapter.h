#pragma once

#include "bytecode/adapters/table_adapter.h"

namespace cpygraph::bytecode {

class Python311Adapter final : public TableDrivenCPythonAdapter {
public:
    using TableDrivenCPythonAdapter::lift;
    Python311Adapter();
    std::string_view version() const noexcept override { return "3.11"; }
};

}  // namespace cpygraph::bytecode
