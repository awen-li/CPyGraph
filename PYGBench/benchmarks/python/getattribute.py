# PYGBENCH: python.getattribute
# CATEGORY: python
# TAGS: dynamic-attribute, hidden-call
# EXPECT: call:run attribute read -> Dynamic.__getattribute__
# FORBID: call:run attribute read -> Dynamic.__getattr__

class Dynamic:
    value = 1

    def __getattribute__(self, name):
        return object.__getattribute__(self, name)

    def __getattr__(self, name):
        return None

def run():
    return Dynamic().value
