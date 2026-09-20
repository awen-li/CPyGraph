# PYGBENCH: ddg.deep_interprocedural_field
# CATEGORY: ddg
# TAGS: field-modeling, interprocedural, multi-hop
# EXPECT: ddg:run value -> write value
# EXPECT: ddg:write value -> write field store
# EXPECT: ddg:write field store -> read field load
# EXPECT: ddg:read field load -> read return
# EXPECT: ddg:read return -> run return
# FORBID: ddg:run return -> read return
# FORBID: ddg:read return -> read field load
# FORBID: ddg:read field load -> write field store
# FORBID: ddg:write field store -> write value
# FORBID: ddg:write value -> run value
# EXPECT_PATH: ddg:run value -> write value -> write field store -> read field load -> read return -> run return
# FORBID_PATH: ddg:run return -> read return -> read field load -> write field store -> write value -> run value

class Box:
    pass


def write(box, value):
    box.payload = value


def read(box):
    return box.payload


def run(value):
    box = Box()
    write(box, value)
    result = read(box)
    return result
