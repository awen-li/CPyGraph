# PYGBENCH: import.cross_module_return
# CATEGORY: import
# TAGS: cross-module, return-flow
# EXPECT: points-to:run item -> model.Item instance from factory.create
# EXPECT: call:run -> consumer.consume
# FORBID: points-to:run item -> model.Decoy instance

from .consumer import consume
from .factory import create

def run(value):
    item = create(value)
    return consume(item)
