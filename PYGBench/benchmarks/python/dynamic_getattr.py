# PYGBENCH: python.dynamic_getattr
# CATEGORY: python
# TAGS: dynamic-attribute, hidden-call
# EXPECT: call:run missing attribute -> Dynamic.__getattr__
# FORBID: call:run existing attribute -> Dynamic.__getattr__

class Dynamic:
    existing = 1

    def __getattr__(self, name):
        return name

def run():
    value = Dynamic()
    return value.existing, value.missing
