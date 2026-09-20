# PYGBENCH: cdg.elif_ladder
# CATEGORY: cdg
# TAGS: control-dependence, branch, branch-outcome, multi-hop
# EXPECT: cdg-true:first condition -> first return
# EXPECT: cdg-false:first condition -> second condition
# EXPECT: cdg-true:second condition -> second return
# EXPECT: cdg-false:second condition -> third condition
# EXPECT: cdg-true:third condition -> third return
# EXPECT: cdg-false:third condition -> fourth condition
# EXPECT: cdg-true:fourth condition -> fourth return
# EXPECT: cdg-false:fourth condition -> fifth return
# FORBID: cdg-false:first condition -> first return
# FORBID: cdg-true:fourth condition -> fifth return

def priority(first, second, third, fourth):
    if first:
        return 1
    elif second:
        return 2
    elif third:
        return 3
    elif fourth:
        return 4
    else:
        return 5
