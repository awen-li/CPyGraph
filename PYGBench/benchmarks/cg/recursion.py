# PYGBENCH: cg.recursion
# CATEGORY: cg
# TAGS: recursion, direct-call
# EXPECT: call:factorial -> factorial
# FORBID: call:factorial -> unrelated

def factorial(value):
    if value <= 1:
        return 1
    return value * factorial(value - 1)

def unrelated():
    return None
