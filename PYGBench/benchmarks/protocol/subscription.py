# PYGBENCH: protocol.subscription
# CATEGORY: protocol
# TAGS: hidden-call, subscription
# EXPECT: call:run -> Store.__setitem__
# EXPECT: call:run -> Store.__getitem__
# FORBID: call:run -> Store.unused

class Store:
    def __setitem__(self, key, value):
        self.value = value

    def __getitem__(self, key):
        return self.value

    def unused(self):
        return None

def run(value):
    store = Store()
    store[0] = value
    return store[0]
