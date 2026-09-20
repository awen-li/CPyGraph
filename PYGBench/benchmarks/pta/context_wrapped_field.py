# PYGBENCH: pta.context_wrapped_field
# CATEGORY: pta
# TAGS: context-sensitive, field-modeling, factory
# EXPECT: points-to:left result -> Left instance
# EXPECT: points-to:right result -> Right instance
# FORBID: points-to:left result -> Right instance
# FORBID: points-to:right result -> Left instance

class Box:
    pass

class Left:
    pass

class Right:
    pass

def wrap(value):
    box = Box()
    box.payload = value
    return box

def left():
    container = wrap(Left())
    return container.payload

def right():
    container = wrap(Right())
    return container.payload
