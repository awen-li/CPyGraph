# PYGBENCH: cdg.boolean_and_chain
# CATEGORY: cdg
# TAGS: control-dependence, branch, boolean, short-circuit, branch-outcome, multi-hop
# EXPECT: cdg-true:first condition -> second condition
# EXPECT: cdg-false:first condition -> false return
# EXPECT: cdg-true:second condition -> third condition
# EXPECT: cdg-false:second condition -> false return
# EXPECT: cdg-true:third condition -> true return
# EXPECT: cdg-false:third condition -> false return
# FORBID: cdg-false:first condition -> second condition
# FORBID: cdg-true:third condition -> false return

def all_enabled(first, second, third):
    if first and second and third:
        return True
    return False
