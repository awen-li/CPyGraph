# PYGBENCH: python.descriptor
# CATEGORY: python
# TAGS: descriptor, hidden-call
# EXPECT: call:run read -> Descriptor.__get__
# EXPECT: call:run write -> Descriptor.__set__
# FORBID: call:run -> Descriptor.unused

class Descriptor:
    def __get__(self, instance, owner):
        return instance._value

    def __set__(self, instance, value):
        instance._value = value

    def unused(self):
        return None

class Owner:
    value = Descriptor()

def run(value):
    owner = Owner()
    owner.value = value
    return owner.value
