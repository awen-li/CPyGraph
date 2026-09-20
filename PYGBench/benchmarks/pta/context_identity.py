# PYGBENCH: pta.context_identity
# CATEGORY: pta
# TAGS: context-sensitive, interprocedural
# EXPECT: points-to:from_left result -> Left instance
# EXPECT: points-to:from_right result -> Right instance
# FORBID: points-to:from_left result -> Right instance
# FORBID: points-to:from_right result -> Left instance

class Left:
    pass

class Right:
    pass

def identity(value):
    return value

def from_left():
    return identity(Left())

def from_right():
    return identity(Right())
