# PYGBENCH: import.cross_module_field
# CATEGORY: import
# TAGS: cross-module, field-modeling
# EXPECT: points-to:reader.read box.payload -> writer.write value
# EXPECT: ddg:writer value -> reader return
# FORBID: points-to:reader.read box.payload -> Box instance

from .model import Box
from .reader import read
from .writer import write

def run(value):
    box = Box()
    write(box, value)
    return read(box)
