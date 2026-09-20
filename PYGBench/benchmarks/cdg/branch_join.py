# PYGBENCH: cdg.branch_join
# CATEGORY: cdg
# TAGS: control-dependence, branch, postdominator, branch-outcome
# EXPECT: cdg-true:compute condition -> compute second assignment
# FORBID: cdg-false:compute condition -> compute second assignment
# FORBID: cdg-true:compute condition -> compute return

def compute(flag):
    value = 0
    if flag:
        value = 1
    return value
