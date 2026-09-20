# PYGBENCH: python.module_getattr
# CATEGORY: python
# TAGS: module, dynamic-attribute
# EXPECT: call:run missing attribute -> __getattr__
# FORBID: call:run_existing existing attribute -> __getattr__

import module_getattr as module

existing = 1

def __getattr__(name):
    return name


def run():
    return module.missing


def run_existing():
    return module.existing
