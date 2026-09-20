# PYGBENCH: ddg.global_def_use
# CATEGORY: ddg
# TAGS: global, interprocedural
# EXPECT: ddg:write parameter -> global stored
# EXPECT: ddg:global stored -> read return
# FORBID: ddg:module object -> read return

stored = None

def write(value):
    global stored
    stored = value

def read():
    return stored
