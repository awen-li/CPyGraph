# PYGBENCH: pta.container_in_field
# CATEGORY: pta
# TAGS: field-modeling, container
# EXPECT: points-to:box.items[0] -> value
# FORBID: points-to:box.items[0] -> Box instance

class Box:
    pass

def store(value):
    box = Box()
    box.items = [value]
    return box.items[0]
