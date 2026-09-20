# PYGBENCH: pta.closure_capture
# CATEGORY: pta
# TAGS: closure, cell
# EXPECT: points-to:inner return -> outer parameter value
# FORBID: points-to:inner return -> inner function

def outer(value):
    def inner():
        return value
    return inner()
