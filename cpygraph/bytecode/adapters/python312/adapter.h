#pragma once

#include "bytecode/adapters/table_adapter.h"

namespace cpygraph::bytecode {

class Python312Adapter final : public TableDrivenCPythonAdapter {
public:
    using TableDrivenCPythonAdapter::lift;
    Python312Adapter();
    std::string_view version() const noexcept override { return "3.12"; }
};

}  // namespace cpygraph::bytecode
