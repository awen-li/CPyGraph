# PYGBENCH: ddg.branch_merge
# CATEGORY: ddg
# TAGS: merge
# EXPECT: ddg:left assignment -> return result
# EXPECT: ddg:right assignment -> return result
# FORBID: ddg:flag -> returned data value

def merge(flag, left, right):
    if flag:
        result = left
    else:
        result = right
    return result
