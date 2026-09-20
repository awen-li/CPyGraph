# PYGBENCH: protocol.hashing
# CATEGORY: protocol
# TAGS: hidden-call, hash
# EXPECT: call:run -> Value.__hash__
# FORBID: call:run -> Value.__eq__

class Value:
    def __hash__(self):
        return 1

    def __eq__(self, other):
        return False

def run(value):
    return hash(value)
