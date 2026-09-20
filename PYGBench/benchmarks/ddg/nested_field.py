# PYGBENCH: ddg.nested_field
# CATEGORY: ddg
# TAGS: field-modeling, access-path
# EXPECT: ddg:value -> outer.inner.payload store
# EXPECT: ddg:outer.inner.payload store -> return
# FORBID: ddg:outer allocation -> payload return

class Node:
    pass

def run(value):
    outer = Node()
    outer.inner = Node()
    outer.inner.payload = value
    return outer.inner.payload
