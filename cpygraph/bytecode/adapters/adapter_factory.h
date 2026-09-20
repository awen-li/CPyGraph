#pragma once

#include "bytecode/adapters/adapter.h"

#include <memory>
#include <string_view>

namespace cpygraph::bytecode {

class AdapterFactory {
public:
    static std::unique_ptr<CPythonAdapter> create(std::string_view version);
};

}  // namespace cpygraph::bytecode
