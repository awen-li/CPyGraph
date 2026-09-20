# PYGBENCH: pta.field_in_container
# CATEGORY: pta
# TAGS: field-modeling, container, alias
# EXPECT: points-to:items[0].payload -> value
# FORBID: points-to:items[0].payload -> items container

class Box:
    pass

def store(value):
    box = Box()
    box.payload = value
    items = [box]
    return items[0].payload
