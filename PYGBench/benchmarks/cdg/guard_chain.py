# PYGBENCH: cdg.guard_chain
# CATEGORY: cdg
# TAGS: control-dependence, branch, nested-control, branch-outcome, multi-hop
# EXPECT: cdg-true:first condition -> first return
# EXPECT: cdg-false:first condition -> second condition
# EXPECT: cdg-true:second condition -> second return
# EXPECT: cdg-false:second condition -> third condition
# EXPECT: cdg-true:third condition -> third return
# EXPECT: cdg-false:third condition -> fourth condition
# EXPECT: cdg-true:fourth condition -> fourth return
# EXPECT: cdg-false:fourth condition -> fifth condition
# EXPECT: cdg-true:fifth condition -> fifth return
# EXPECT: cdg-false:fifth condition -> sixth return
# FORBID: cdg-false:first condition -> first return
# FORBID: cdg-true:fifth condition -> sixth return

def classify(first, second, third, fourth, fifth):
    if first:
        return 1
    if second:
        return 2
    if third:
        return 3
    if fourth:
        return 4
    if fifth:
        return 5
    return 6
