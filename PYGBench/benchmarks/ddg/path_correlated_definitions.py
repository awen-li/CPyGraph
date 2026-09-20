# PYGBENCH: ddg.path_correlated_definitions
# CATEGORY: ddg
# TAGS: correlation
# EXPECT: ddg:true assignment -> true return
# EXPECT: ddg:false assignment -> false return
# FORBID: ddg:true assignment -> false return
# FORBID: ddg:false assignment -> true return

def run(flag, left, right):
    if flag:
        value = left
    else:
        value = right
    if flag:
        return value
    return value
