# PYGBENCH: ddg.strong_field_update
# CATEGORY: ddg
# TAGS: field-modeling, strong-update
# EXPECT: ddg:replacement -> return box.payload
# FORBID: ddg:original -> return box.payload

class Box:
    pass

def run(original, replacement):
    box = Box()
    box.payload = original
    box.payload = replacement
    return box.payload
