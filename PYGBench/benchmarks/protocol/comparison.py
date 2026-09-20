# PYGBENCH: protocol.comparison
# CATEGORY: protocol
# TAGS: hidden-call, comparison
# EXPECT: call:run -> Value.__lt__
# EXPECT: call:run -> Value.__eq__
# FORBID: call:run -> Value.__gt__

class Value:
    def __lt__(self, other):
        return False

    def __eq__(self, other):
        return True

    def __gt__(self, other):
        return False

def run():
    left = Value()
    right = Value()
    return left < right, left == right
