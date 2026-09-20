# PYGBENCH: cg.context_callback
# CATEGORY: cg
# TAGS: context-sensitive, higher-order
# EXPECT: call:invoke@run_left -> left
# EXPECT: call:invoke@run_right -> right
# FORBID: call:invoke@run_left -> right
# FORBID: call:invoke@run_right -> left

def left():
    return 1

def right():
    return 2

def invoke(callback):
    return callback()

def run_left():
    return invoke(left)

def run_right():
    return invoke(right)
