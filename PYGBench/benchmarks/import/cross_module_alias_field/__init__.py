# PYGBENCH: import.cross_module_alias_field
# CATEGORY: import
# TAGS: cross-module, alias, field-modeling
# EXPECT: points-to:alias.Record.payload -> model.Record.payload
# FORBID: points-to:alias.Record.payload -> model.Decoy.payload

from .model import Record as Alias

def run(value):
    item = Alias()
    item.payload = value
    return item.payload
