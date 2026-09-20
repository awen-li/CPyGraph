# PYGBENCH: import.relative_from
# CATEGORY: import
# TAGS: package, relative
# EXPECT: call:run -> provider.target
# FORBID: call:run -> provider.decoy

from .provider import target

def run(value):
    return target(value)
