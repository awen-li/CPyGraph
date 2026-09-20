# PYGBENCH: pta.instance_field
# CATEGORY: pta
# TAGS: field-modeling, heap
# EXPECT: points-to:read box.payload -> parameter payload
# FORBID: points-to:read box.payload -> Box instance

class Box:
    pass

def round_trip(payload):
    box = Box()
    box.payload = payload
    return box.payload
