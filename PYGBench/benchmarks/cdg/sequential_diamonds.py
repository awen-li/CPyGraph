# PYGBENCH: cdg.sequential_diamonds
# CATEGORY: cdg
# TAGS: control-dependence, branch, branch-outcome, postdominator
# EXPECT: cdg-true:first condition -> first assignment
# EXPECT: cdg-false:first condition -> second assignment
# EXPECT: cdg-true:second condition -> third assignment
# EXPECT: cdg-false:second condition -> fourth assignment
# FORBID: cdg-true:first condition -> third assignment
# FORBID: cdg-false:second condition -> second assignment

def combine(first, second):
    if first:
        left = 1
    else:
        left = 2
    if second:
        right = 3
    else:
        right = 4
    return left + right
