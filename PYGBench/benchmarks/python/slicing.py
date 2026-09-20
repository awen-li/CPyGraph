# PYGBENCH: python.slicing
# CATEGORY: python
# TAGS: subscription, slice
# EXPECT: call:run -> Sequence.__getitem__ with slice
# FORBID: call:run -> Sequence.__setitem__

class Sequence:
    def __getitem__(self, key):
        return key

    def __setitem__(self, key, value):
        return None

def run(value):
    return value[1:4:2]
