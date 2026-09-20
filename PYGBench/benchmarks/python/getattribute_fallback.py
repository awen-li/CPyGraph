# PYGBENCH: python.getattribute_fallback
# CATEGORY: python
# TAGS: dynamic-attribute, exception, hidden-call
# EXPECT: call:run attribute read -> Dynamic.__getattribute__
# EXPECT: call:run attribute read -> Dynamic.__getattr__
# FORBID: call:run attribute read -> Dynamic.unrelated

class Dynamic:
    value = 1

    def __getattribute__(self, name):
        raise AttributeError(name)

    def __getattr__(self, name):
        return name

    def unrelated(self):
        return None


def run():
    return Dynamic().value
