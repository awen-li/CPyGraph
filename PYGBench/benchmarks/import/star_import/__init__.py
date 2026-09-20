# PYGBENCH: import.star_import
# CATEGORY: import
# TAGS: package, star-import
# EXPECT: call:run -> provider.exported
# FORBID: call:run -> provider.hidden

from .provider import *

def run(value):
    return exported(value)
