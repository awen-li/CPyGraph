# PYGBENCH: negative.unused_protocol
# CATEGORY: negative
# TAGS: precision, hidden-call
# EXPECT: call:run -> Value.__add__
# FORBID: call:run -> Value.__sub__

class Value:
    def __add__(self, other):
        return self

    def __sub__(self, other):
        return self

def run(left, right):
    return left + right
