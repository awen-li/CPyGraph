# PYGBENCH: cdg.if_else_returns
# CATEGORY: cdg
# TAGS: control-dependence, branch, postdominator, branch-outcome
# EXPECT: cdg-true:choose condition -> choose true return
# EXPECT: cdg-false:choose condition -> choose false return
# FORBID: cdg-true:choose condition -> choose false return
# FORBID: cdg-false:choose condition -> choose true return

def choose(flag):
    if flag:
        return 1
    return 2
