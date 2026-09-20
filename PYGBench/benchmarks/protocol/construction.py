# PYGBENCH: protocol.construction
# CATEGORY: protocol
# TAGS: hidden-call, constructor
# EXPECT: call:run -> Item.__init__
# FORBID: call:run -> Item.unused

class Item:
    def __init__(self, value):
        self.value = value

    def unused(self):
        return None

def run(value):
    return Item(value)
