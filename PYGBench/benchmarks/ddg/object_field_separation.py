# PYGBENCH: ddg.object_field_separation
# CATEGORY: ddg
# TAGS: field-modeling, object-separation
# EXPECT: ddg:first -> left.payload return
# EXPECT: ddg:second -> right.payload return
# FORBID: ddg:second -> left.payload return

class Box:
    pass

def run(first, second):
    left = Box()
    right = Box()
    left.payload = first
    right.payload = second
    return left.payload, right.payload
