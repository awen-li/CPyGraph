# PYGBENCH: ddg.aliased_field
# CATEGORY: ddg
# TAGS: field-modeling, base-alias
# EXPECT: ddg:value -> alias.payload store
# EXPECT: ddg:alias.payload store -> box.payload return
# FORBID: ddg:Box allocation -> box.payload return

class Box:
    pass

def run(value):
    box = Box()
    alias = box
    alias.payload = value
    return box.payload
