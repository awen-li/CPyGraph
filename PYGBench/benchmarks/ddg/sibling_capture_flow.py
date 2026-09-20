# PYGBENCH: ddg.sibling_capture_flow
# CATEGORY: ddg
# TAGS: closure, nonlocal, cell, nested-code, interprocedural
# EXPECT: ddg:write nonlocal update -> read return
# FORBID: ddg:write function object -> read return

def outer(initial, replacement):
    value = initial

    def write():
        nonlocal value
        value = replacement

    def read():
        return value

    write()
    return read()
