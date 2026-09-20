# PYGBENCH: python.descriptor_inheritance
# CATEGORY: python
# TAGS: descriptor, inheritance, field-modeling
# EXPECT: call:run Child.value read -> Descriptor.__get__
# EXPECT: call:run Child.value write -> Descriptor.__set__
# FORBID: points-to:Child.value -> unrelated

class Descriptor:
    def __get__(self, instance, owner):
        return instance._value

    def __set__(self, instance, value):
        instance._value = value

class Base:
    value = Descriptor()

class Child(Base):
    pass

def run(value):
    child = Child()
    child.value = value
    return child.value
