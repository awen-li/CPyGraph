#pragma once

// Low-overhead process profiling is enabled by default. Measurements use
// numeric phase IDs internally and retain no strings unless a client renders
// their names. Applications may explicitly disable collection.

#include "analysis/profile/profiler.h"
