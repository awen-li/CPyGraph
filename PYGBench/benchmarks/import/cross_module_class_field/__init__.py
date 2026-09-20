# PYGBENCH: import.cross_module_class_field
# CATEGORY: import
# TAGS: cross-module, class-attribute, field-modeling
# EXPECT: points-to:reader.read Item.shared -> run value
# FORBID: points-to:reader.read Item.shared -> Item instance

from .model import Item
from .reader import read

def run(value):
    Item.shared = value
    return read()
