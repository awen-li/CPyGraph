# PYGBENCH: pta.same_object_fields
# CATEGORY: pta
# TAGS: field-modeling, field-separation
# EXPECT: points-to:box.left -> first
# EXPECT: points-to:box.right -> second
# FORBID: points-to:box.left -> second
# FORBID: points-to:box.right -> first

class Box:
    pass

def store(first, second):
    box = Box()
    box.left = first
    box.right = second
    return box.left, box.right
