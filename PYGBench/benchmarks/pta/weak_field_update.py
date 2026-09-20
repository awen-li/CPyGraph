# PYGBENCH: pta.weak_field_update
# CATEGORY: pta
# TAGS: field-modeling, weak-update, alias
# EXPECT: points-to:left.payload -> original
# EXPECT: points-to:left.payload -> replacement
# FORBID: points-to:left.payload -> condition

class Box:
    pass

def update(condition, original, replacement):
    left = Box()
    right = Box()
    left.payload = original
    right.payload = original
    target = left if condition else right
    target.payload = replacement
    return left.payload
