# PYGBENCH: import.alias_and_attribute
# CATEGORY: import
# TAGS: package, alias
# EXPECT: call:call_imports first alias site -> helper.imported
# EXPECT: call:call_imports second module attribute site -> helper.imported
# FORBID: call:call_imports first alias site -> local_decoy
# FORBID: call:call_imports second module attribute site -> local_decoy

from .helper import imported as alias
from . import helper


def local_decoy(value):
    return value


def call_imports(value):
    alias(value)
    helper.imported(value)
