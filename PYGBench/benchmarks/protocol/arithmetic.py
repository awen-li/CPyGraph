# PYGBENCH: protocol.arithmetic
# CATEGORY: protocol
# TAGS: hidden-call, operator
# EXPECT: call:run -> Number.__add__
# EXPECT: call:run -> Number.__mul__
# FORBID: call:run -> Number.__sub__

class Number:
    def __add__(self, other):
        return self

    def __mul__(self, other):
        return self

    def __sub__(self, other):
        return self

def run(left, right):
    return left + right * left
