# PYGBENCH: python.classmethod_staticmethod
# CATEGORY: python
# TAGS: descriptor, method-binding
# EXPECT: call:run -> Factory.create
# EXPECT: call:run -> Factory.normalize
# FORBID: call:run -> Factory.unused

class Factory:
    @classmethod
    def create(cls, value):
        return cls(value)

    @staticmethod
    def normalize(value):
        return value

    def __init__(self, value):
        self.value = value

    def unused(self):
        return None

def run(value):
    return Factory.create(Factory.normalize(value))
