# PYGBENCH: protocol.comparison_not_implemented
# CATEGORY: protocol
# TAGS: comparison, hidden-call, not-implemented
# EXPECT: call:run -> Left.__eq__
# EXPECT: call:run -> Right.__eq__
# FORBID: call:run -> Right.unused

class Left:
    def __eq__(self, other):
        return NotImplemented


class Right:
    def __eq__(self, other):
        return True

    def unused(self):
        return False


def run():
    return Left() == Right()
