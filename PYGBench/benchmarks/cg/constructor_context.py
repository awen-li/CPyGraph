# PYGBENCH: cg.constructor_context
# CATEGORY: cg
# TAGS: context-sensitive, constructor, higher-order
# EXPECT: call:create@left -> Left.__init__
# EXPECT: call:create@right -> Right.__init__
# FORBID: call:create@left -> Right.__init__

class Left:
    def __init__(self):
        self.side = "left"

class Right:
    def __init__(self):
        self.side = "right"

def create(constructor):
    return constructor()

def left():
    return create(Left)

def right():
    return create(Right)
