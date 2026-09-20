# PYGBENCH: cfg.branch
# CATEGORY: cfg
# TAGS: flow-sensitive, branch
# EXPECT: cfg:choose entry -> true-return
# EXPECT: cfg:choose entry -> false-return
# FORBID: cfg:choose true-return -> false-return

def choose(flag, left, right):
    if flag:
        return left
    return right
