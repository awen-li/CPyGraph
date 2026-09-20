# PYGBENCH: python.nonlocal_closure
# CATEGORY: python
# TAGS: closure, nonlocal
# EXPECT: ddg:increment nonlocal update -> later increment read
# EXPECT: points-to:make_counter return -> increment closure
# FORBID: points-to:make_counter return -> make_counter

def make_counter():
    value = 0
    def increment():
        nonlocal value
        value += 1
        return value
    return increment
