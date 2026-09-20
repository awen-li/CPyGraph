#pragma once

// Public bytecode-adapter API. Clients select a target CPython version through
// AdapterFactory and use the returned adapter to parse version-native code and
// exception metadata. Concrete version adapters are internal extension points.

#include "bytecode/adapters/adapter.h"
#include "bytecode/adapters/adapter_factory.h"
#include "bytecode/adapters/table_adapter.h"
#include "bytecode/code_object_loader.h"
#include "bytecode/raw_bytecode.h"
