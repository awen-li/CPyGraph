# PYGBENCH: protocol.conversions
# CATEGORY: protocol
# TAGS: hidden-call, conversion
# EXPECT: call:run -> Value.__str__
# EXPECT: call:run -> Value.__int__
# EXPECT: call:run -> Value.__float__
# FORBID: call:run -> Value.unused

class Value:
    def __str__(self):
        return "value"

    def __int__(self):
        return 1

    def __float__(self):
        return 1.0

    def unused(self):
        return None

def run(value):
    return str(value), int(value), float(value)
