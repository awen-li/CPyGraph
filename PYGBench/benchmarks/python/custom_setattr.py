# PYGBENCH: python.custom_setattr
# CATEGORY: python
# TAGS: hidden-call, dynamic-attribute
# EXPECT: call:run assignment -> Item.__setattr__
# FORBID: call:run -> Item.unused

class Item:
    def __setattr__(self, name, value):
        object.__setattr__(self, name, value)

    def unused(self):
        return None

def run(value):
    item = Item()
    item.payload = value
    return item.payload
