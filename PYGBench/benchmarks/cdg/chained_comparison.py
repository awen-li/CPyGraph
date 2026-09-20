# PYGBENCH: cdg.chained_comparison
# CATEGORY: cdg
# TAGS: control-dependence, branch, chained-comparison, branch-outcome, multi-hop
# EXPECT: cdg-true:first condition -> second condition
# EXPECT: cdg-true:second condition -> true return
# EXPECT: cdg-false:second condition -> false return
# EXPECT: 3.11..3.11 | cdg-false:first condition -> false return
# FORBID: cdg-false:first condition -> second condition
# FORBID: 3.10..3.10 | cdg-false:first condition -> false return
# FORBID: 3.12..3.14 | cdg-false:first condition -> false return
# FORBID: cdg:true return -> second condition

def within(lower, value, upper):
    if lower < value < upper:
        return True
    return False
