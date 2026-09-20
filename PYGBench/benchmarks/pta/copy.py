# PYGBENCH: pta.copy
# CATEGORY: pta
# TAGS: inclusion, local
# EXPECT: points-to:alias -> parameter value
# FORBID: points-to:alias -> unrelated object

def copy(value):
    alias = value
    return alias
