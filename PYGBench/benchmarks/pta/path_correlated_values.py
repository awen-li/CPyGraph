# PYGBENCH: pta.path_correlated_values
# CATEGORY: pta
# TAGS: path-sensitive, flow-sensitive, correlation
# EXPECT: points-to:true return -> left
# EXPECT: points-to:false return -> right
# FORBID: points-to:true return -> right
# FORBID: points-to:false return -> left

def choose(flag, left, right):
    value = left if flag else right
    if flag:
        return value
    return value
