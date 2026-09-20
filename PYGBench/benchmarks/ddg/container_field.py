# PYGBENCH: ddg.container_field
# CATEGORY: ddg
# TAGS: field-modeling, container
# EXPECT: ddg:value -> box.items element
# EXPECT: ddg:box.items element -> return
# FORBID: ddg:box allocation -> return payload

class Box:
    pass

def run(value):
    box = Box()
    box.items = []
    box.items.append(value)
    return box.items[0]
