# PYGBENCH: python.custom_delattr
# CATEGORY: python
# TAGS: hidden-call, dynamic-attribute
# EXPECT: call:run deletion -> Item.__delattr__
# FORBID: call:run deletion -> Item.unused

class Item:
    def __delattr__(self, name):
        object.__delattr__(self, name)

    def unused(self):
        return None

def run(item):
    del item.payload
