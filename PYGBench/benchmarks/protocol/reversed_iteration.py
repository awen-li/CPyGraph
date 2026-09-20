# PYGBENCH: protocol.reversed_iteration
# CATEGORY: protocol
# TAGS: hidden-call, iterator, reversed
# EXPECT: call:run -> Values.__reversed__
# FORBID: call:run -> Values.__iter__

class Values:
    def __reversed__(self):
        return iter(())

    def __iter__(self):
        return iter(())

def run(values):
    return reversed(values)
