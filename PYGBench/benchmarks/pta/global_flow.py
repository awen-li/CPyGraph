# PYGBENCH: pta.global_flow
# CATEGORY: pta
# TAGS: global, interprocedural
# EXPECT: points-to:read_global return -> write_global parameter
# FORBID: points-to:read_global return -> module object

stored = None

def write_global(value):
    global stored
    stored = value

def read_global():
    return stored
