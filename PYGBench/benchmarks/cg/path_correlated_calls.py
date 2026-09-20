# PYGBENCH: cg.path_correlated_calls
# CATEGORY: cg
# TAGS: path-sensitive, flow-sensitive, correlation
# EXPECT: call:true call site -> left
# EXPECT: call:false call site -> right
# FORBID: call:true call site -> right
# FORBID: call:false call site -> left

def left():
    return 1

def right():
    return 2

def run(flag):
    target = left if flag else right
    if flag:
        return target()
    return target()
