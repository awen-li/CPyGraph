# PYGBENCH: import.cross_module_ddg
# CATEGORY: import
# TAGS: cross-module, data-flow
# EXPECT: ddg:run parameter -> transform parameter
# EXPECT: ddg:transform return -> sink.consume parameter
# FORBID: ddg:decoy return -> sink.consume parameter

from .sink import consume
from .transform import transform

def decoy(value):
    return value

def run(value):
    return consume(transform(value))
