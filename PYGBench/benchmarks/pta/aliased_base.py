# PYGBENCH: pta.aliased_base
# CATEGORY: pta
# TAGS: field-modeling, base-alias
# EXPECT: points-to:alias.payload -> value
# EXPECT: points-to:box.payload -> value
# FORBID: points-to:alias.payload -> Box instance

class Box:
    pass

def store(value):
    box = Box()
    alias = box
    alias.payload = value
    return box.payload
