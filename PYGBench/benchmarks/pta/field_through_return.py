# PYGBENCH: pta.field_through_return
# CATEGORY: pta
# TAGS: field-modeling, interprocedural, return
# EXPECT: points-to:run box.payload -> create parameter value
# FORBID: points-to:run box.payload -> create function

class Box:
    pass

def create(value):
    box = Box()
    box.payload = value
    return box

def run(value):
    box = create(value)
    return box.payload
