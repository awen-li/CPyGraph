# PYGBENCH: python.chained_comparison
# CATEGORY: python
# TAGS: comparison, control-flow
# EXPECT: call:run -> Value.__lt__ at both comparisons
# FORBID: call:run -> Value.__gt__

class Value:
    def __lt__(self, other):
        return True

    def __gt__(self, other):
        return False

def run():
    first = Value()
    second = Value()
    third = Value()
    return first < second < third
