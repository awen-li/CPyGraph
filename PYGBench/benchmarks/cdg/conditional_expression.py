# PYGBENCH: cdg.conditional_expression
# CATEGORY: cdg
# TAGS: control-dependence, branch, conditional-expression, branch-outcome
# EXPECT: cdg-true:choose condition -> choose first call
# EXPECT: cdg-false:choose condition -> choose second call
# EXPECT: ddg:choose call -> choose return
# FORBID: cdg-false:choose condition -> choose first call
# FORBID: cdg-true:choose condition -> choose second call
# FORBID: ddg:choose return -> choose call

def choose(flag, on_true, on_false):
    return on_true() if flag else on_false()
