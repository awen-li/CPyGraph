# PYGBENCH: protocol.reflected_operator
# CATEGORY: protocol
# TAGS: hidden-call, reflected-operator
# EXPECT: call:run -> Right.__radd__
# FORBID: call:run -> Right.__add__

class Left:
    pass

class Right:
    def __radd__(self, other):
        return self

    def __add__(self, other):
        return self

def run():
    return Left() + Right()
