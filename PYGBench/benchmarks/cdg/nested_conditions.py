# PYGBENCH: cdg.nested_conditions
# CATEGORY: cdg
# TAGS: control-dependence, branch, nested-control, branch-outcome
# EXPECT: cdg-true:first condition -> second condition
# EXPECT: cdg-false:first condition -> select false return
# EXPECT: cdg-true:second condition -> select true return
# EXPECT: cdg-false:second condition -> select sequence return
# FORBID: cdg-true:second condition -> select false return
# EXPECT_PATH: cdg:first condition -> second condition -> select true return

def select(first, second):
    if first:
        if second:
            return 1
        return 2
    return 3
