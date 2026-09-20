# PYGBENCH: protocol.unary_operators
# CATEGORY: protocol
# TAGS: hidden-call, operator
# EXPECT: call:run -> Value.__neg__
# EXPECT: call:run -> Value.__invert__
# FORBID: call:run -> Value.__pos__

class Value:
    def __neg__(self):
        return self

    def __invert__(self):
        return self

    def __pos__(self):
        return self

def run(value):
    return -value, ~value
