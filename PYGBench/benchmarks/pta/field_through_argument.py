# PYGBENCH: pta.field_through_argument
# CATEGORY: pta
# TAGS: field-modeling, interprocedural, argument
# EXPECT: points-to:read parameter box.payload -> write parameter value
# FORBID: points-to:read return -> Box instance

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
