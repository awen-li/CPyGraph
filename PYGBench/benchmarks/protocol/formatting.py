# PYGBENCH: protocol.formatting
# CATEGORY: protocol
# TAGS: hidden-call, formatting
# EXPECT: call:run -> Value.__format__
# FORBID: call:run -> Value.__repr__

class Value:
    def __format__(self, specification):
        return specification

    def __repr__(self):
        return "unused"

def run(value):
    return format(value, ">10")
