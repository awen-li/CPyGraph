# PYGBENCH: cdg.decision_tree
# CATEGORY: cdg
# TAGS: control-dependence, branch, nested-control, branch-outcome, multi-hop
# EXPECT: cdg-true:first condition -> second condition
# EXPECT: cdg-false:first condition -> fifth condition
# EXPECT: cdg-true:second condition -> third condition
# EXPECT: cdg-false:second condition -> fourth condition
# EXPECT: cdg-true:third condition -> first return
# EXPECT: cdg-false:third condition -> second return
# EXPECT: cdg-true:fourth condition -> third return
# EXPECT: cdg-false:fourth condition -> fourth return
# EXPECT: cdg-true:fifth condition -> sixth condition
# EXPECT: cdg-false:fifth condition -> seventh condition
# EXPECT: cdg-true:sixth condition -> fifth return
# EXPECT: cdg-false:sixth condition -> sixth return
# EXPECT: cdg-true:seventh condition -> seventh return
# EXPECT: cdg-false:seventh condition -> eighth return
# FORBID: cdg-false:first condition -> second condition
# FORBID: cdg-true:first condition -> fifth condition

def classify(first, second, third):
    if first:
        if second:
            if third:
                return 1
            return 2
        if third:
            return 3
        return 4
    if second:
        if third:
            return 5
        return 6
    if third:
        return 7
    return 8
