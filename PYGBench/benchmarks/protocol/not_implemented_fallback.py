# PYGBENCH: protocol.not_implemented_fallback
# CATEGORY: protocol
# TAGS: hidden-call, reflected-operator, not-implemented
# EXPECT: call:run -> Left.__add__
# EXPECT: call:run -> Right.__radd__
# FORBID: call:run -> Right.__add__

class Left:
    def __add__(self, other):
        return NotImplemented


class Right:
    def __radd__(self, other):
        return self

    def __add__(self, other):
        return self


def run():
    return Left() + Right()
