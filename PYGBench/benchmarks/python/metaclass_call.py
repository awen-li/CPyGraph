# PYGBENCH: python.metaclass_call
# CATEGORY: python
# TAGS: metaclass, hidden-call
# EXPECT: call:run -> Meta.__call__
# EXPECT: call:Meta.__call__ -> Item.__init__
# FORBID: call:run -> Item.unused
# FORBID: call:Meta.__call__ -> Item.unused

class Meta(type):
    def __call__(cls, value):
        return super().__call__(value)

class Item(metaclass=Meta):
    def __init__(self, value):
        self.value = value

    def unused(self):
        return None

def run(value):
    return Item(value)
