# PYGBENCH: cdg.loop_control
# CATEGORY: cdg
# TAGS: control-dependence, loop, postdominator, branch-outcome
# EXPECT: cdg-true:count condition -> count loop body
# EXPECT: cdg-true:count condition -> count condition
# FORBID: cdg-false:count condition -> count loop body
# FORBID: cdg:count return -> count condition

def count(limit):
    value = 0
    while value < limit:
        value += 1
    return value
