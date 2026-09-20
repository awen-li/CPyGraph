# PYGBENCH: pta.container_flow
# CATEGORY: pta
# TAGS: container, heap
# EXPECT: points-to:items[0] -> parameter value
# FORBID: points-to:items[0] -> list object

def store(value):
    items = [value]
    return items[0]
