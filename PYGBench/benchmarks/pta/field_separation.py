# PYGBENCH: pta.field_separation
# CATEGORY: pta
# TAGS: field-modeling, negative
# EXPECT: points-to:read left.payload -> first
# EXPECT: points-to:read right.payload -> second
# FORBID: points-to:read left.payload -> second

class Cell:
    pass

def separate(first, second):
    left = Cell()
    right = Cell()
    left.payload = first
    right.payload = second
    return left.payload, right.payload
