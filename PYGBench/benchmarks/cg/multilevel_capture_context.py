# PYGBENCH: cg.multilevel_capture_context
# CATEGORY: cg
# TAGS: context-sensitive, closure, nested-code, interprocedural, two-call-site
# EXPECT: call:leaf@left_entry -> left
# EXPECT: call:leaf@right_entry -> right
# FORBID: call:leaf@left_entry -> right
# FORBID: call:leaf@right_entry -> left

def left():
    return 1

def right():
    return 2

def bind(callback):
    def middle():
        def leaf():
            return callback()
        return leaf
    return middle()

def left_entry():
    leaf = bind(left)
    return leaf()

def right_entry():
    leaf = bind(right)
    return leaf()
