# PYGBENCH: import.module_alias
# CATEGORY: import
# TAGS: package, alias
# EXPECT: call:run -> provider.target
# FORBID: call:run -> provider.decoy

from . import provider as service

def run(value):
    return service.target(value)
