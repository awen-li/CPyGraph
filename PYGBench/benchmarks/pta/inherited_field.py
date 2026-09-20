# PYGBENCH: pta.inherited_field
# CATEGORY: pta
# TAGS: field-modeling, inheritance
# EXPECT: points-to:Child.payload -> Base.payload value
# EXPECT: points-to:child.own -> instance value
# FORBID: points-to:Base.payload -> child.own value

class Base:
    payload = None

class Child(Base):
    pass

def read(class_value, instance_value):
    Base.payload = class_value
    child = Child()
    child.own = instance_value
    return child.payload, child.own
