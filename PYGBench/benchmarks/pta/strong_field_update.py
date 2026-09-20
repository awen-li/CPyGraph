# PYGBENCH: pta.strong_field_update
# CATEGORY: pta
# TAGS: field-modeling, strong-update, precision
# EXPECT: points-to:box.payload at return -> replacement
# FORBID: points-to:box.payload at return -> original

class Box:
    pass

def replace(original, replacement):
    box = Box()
    box.payload = original
    box.payload = replacement
    return box.payload
