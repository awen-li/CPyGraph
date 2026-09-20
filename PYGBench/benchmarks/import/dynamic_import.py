# PYGBENCH: import.dynamic_import
# CATEGORY: import
# TAGS: dynamic-import, unresolved-group
# FORBID: call:run -> local decoy

import importlib

def decoy():
    return None

def run(name):
    return importlib.import_module(name)
