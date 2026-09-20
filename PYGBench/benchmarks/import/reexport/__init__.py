# PYGBENCH: import.reexport
# CATEGORY: import
# TAGS: package, reexport
# EXPECT: call:run -> api.target
# FORBID: call:run -> api.decoy

from .api import target

def run(value):
    return target(value)
