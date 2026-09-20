# PYGBENCH: cdg.boolean_or_chain
# CATEGORY: cdg
# TAGS: control-dependence, branch, boolean, short-circuit, branch-outcome, multi-hop
# EXPECT: cdg-false:first condition -> second condition
# EXPECT: cdg-true:first condition -> true return
# EXPECT: cdg-false:second condition -> third condition
# EXPECT: cdg-true:second condition -> true return
# EXPECT: cdg-true:third condition -> true return
# EXPECT: cdg-false:third condition -> false return
# FORBID: cdg-true:first condition -> second condition
# FORBID: cdg-false:third condition -> true return

def any_enabled(first, second, third):
    if first or second or third:
        return True
    return False
