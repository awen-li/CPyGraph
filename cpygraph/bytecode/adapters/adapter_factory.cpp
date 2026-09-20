#include "bytecode/adapters/adapter_factory.h"

#include "cpygraph_adapter_registry_includes.inc"

#include <stdexcept>

namespace cpygraph::bytecode {

std::unique_ptr<CPythonAdapter> AdapterFactory::create(std::string_view version) {
#include "cpygraph_adapter_registry_entries.inc"
    throw std::invalid_argument("unsupported CPython bytecode version: " + std::string(version));
}

}  // namespace cpygraph::bytecode
