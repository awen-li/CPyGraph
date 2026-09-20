# PYGBENCH: cdg.nested_assignments
# CATEGORY: cdg
# TAGS: control-dependence, branch, nested-control, branch-outcome, postdominator
# EXPECT: cdg-true:first condition -> second condition
# EXPECT: cdg-false:first condition -> third assignment
# EXPECT: cdg-true:second condition -> first assignment
# EXPECT: cdg-false:second condition -> second assignment
# FORBID: cdg-false:first condition -> second condition
# FORBID: cdg-true:second condition -> third assignment

def choose(first, second):
    if first:
        if second:
            value = 1
        else:
            value = 2
    else:
        value = 3
    return value
