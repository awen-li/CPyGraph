# PYGBENCH: cdg.match_ladder
# CATEGORY: cdg
# TAGS: control-dependence, branch, pattern-matching, branch-outcome, multi-hop
# EXPECT: cdg-true:first condition -> first return
# EXPECT: cdg-false:first condition -> second condition
# EXPECT: cdg-true:second condition -> second return
# EXPECT: cdg-false:second condition -> third condition
# EXPECT: cdg-true:third condition -> third return
# EXPECT: cdg-false:third condition -> fourth return
# FORBID: cdg-false:first condition -> first return
# FORBID: cdg-true:third condition -> fourth return

def classify(value):
    match value:
        case 0:
            return "zero"
        case 1:
            return "one"
        case 2:
            return "two"
        case _:
            return "other"
