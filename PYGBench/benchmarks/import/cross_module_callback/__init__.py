# PYGBENCH: import.cross_module_callback
# CATEGORY: import
# TAGS: cross-module, higher-order
# EXPECT: call:run -> dispatcher.invoke
# EXPECT: call:dispatcher.invoke -> callbacks.target
# FORBID: call:dispatcher.invoke -> callbacks.decoy

from .callbacks import target
from .dispatcher import invoke

def run(value):
    return invoke(target, value)
