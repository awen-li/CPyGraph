# PYGBENCH: pta.context_recursive_identity
# CATEGORY: pta
# TAGS: context-sensitive, recursion, interprocedural
# EXPECT: points-to:left result -> Left instance
# EXPECT: points-to:right result -> Right instance
# FORBID: points-to:left result -> Right instance
# FORBID: points-to:right result -> Left instance

class Left:
    pass

class Right:
    pass

def carry(value, depth):
    if depth:
        return carry(value, depth - 1)
    return value

def left():
    return carry(Left(), 1)

def right():
    return carry(Right(), 1)
