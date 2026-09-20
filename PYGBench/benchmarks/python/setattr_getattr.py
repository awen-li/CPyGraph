# PYGBENCH: python.setattr_getattr
# CATEGORY: python
# TAGS: dynamic-attribute, field-modeling
# EXPECT: points-to:getattr box payload -> value
# FORBID: points-to:getattr box other -> value

class Box:
    pass

def run(value):
    box = Box()
    setattr(box, "payload", value)
    return getattr(box, "payload")
