# PYGBENCH: ddg.interprocedural_field
# CATEGORY: ddg
# TAGS: field-modeling, interprocedural
# EXPECT: ddg:run value -> write value
# EXPECT: ddg:write field store -> read field load
# EXPECT: ddg:read return -> run return
# FORBID: ddg:Box allocation -> run return

class Box:
    pass

def write(box, value):
    box.payload = value

def read(box):
    return box.payload

def run(value):
    box = Box()
    write(box, value)
    return read(box)
