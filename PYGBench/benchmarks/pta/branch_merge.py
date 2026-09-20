# PYGBENCH: pta.branch_merge
# CATEGORY: pta
# TAGS: flow-sensitive, merge
# EXPECT: points-to:result -> left
# EXPECT: points-to:result -> right
# FORBID: points-to:result -> flag

def choose(flag, left, right):
    result = left
    if flag:
        result = right
    return result
