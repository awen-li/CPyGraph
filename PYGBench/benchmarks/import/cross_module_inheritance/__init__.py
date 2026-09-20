# PYGBENCH: import.cross_module_inheritance
# CATEGORY: import
# TAGS: cross-module, inheritance, super
# EXPECT: call:run -> child.Child.work
# EXPECT: call:child.Child.work -> base.Base.work
# FORBID: call:run -> base.Base.work directly

from .child import Child

def run():
    return Child().work()
