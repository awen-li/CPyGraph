# PYGBENCH: cg.branch_targets
# CATEGORY: cg
# TAGS: indirect-call, flow-sensitive
# EXPECT: call:caller -> left
# EXPECT: call:caller -> right
# FORBID: call:caller -> caller

def left():
    return 1

def right():
    return 2

def caller(flag):
    target = left if flag else right
    return target()
