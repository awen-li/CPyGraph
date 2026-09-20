# PYGBENCH: cg.closure_context
# CATEGORY: cg
# TAGS: context-sensitive, closure
# EXPECT: call:left closure -> left
# EXPECT: call:right closure -> right
# FORBID: call:left closure -> right
# FORBID: call:right closure -> left

def left():
    return 1

def right():
    return 2

def bind(callback):
    def closure():
        return callback()
    return closure

def run():
    left_closure = bind(left)
    right_closure = bind(right)
    return left_closure(), right_closure()
