# PYGBENCH: python.augmented_assignment
# CATEGORY: python
# TAGS: operator, assignment
# EXPECT: call:run -> Value.__iadd__
# FORBID: call:run -> Value.__isub__

class Value:
    def __iadd__(self, other):
        return self

    def __isub__(self, other):
        return self

def run(value, other):
    value += other
    return value
