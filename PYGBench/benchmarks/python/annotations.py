# PYGBENCH: python.annotations
# CATEGORY: python
# TAGS: annotations, function-definition
# EXPECT: ddg:value -> identity return
# FORBID: call:identity -> Marker

class Marker:
    pass

def identity(value: Marker) -> Marker:
    return value
