# PYGBENCH: pta.nested_fields
# CATEGORY: pta
# TAGS: field-modeling, access-path
# EXPECT: points-to:outer.inner.payload -> value
# FORBID: points-to:outer.payload -> value

class Node:
    pass

def store(value):
    outer = Node()
    outer.inner = Node()
    outer.inner.payload = value
    return outer.inner.payload
