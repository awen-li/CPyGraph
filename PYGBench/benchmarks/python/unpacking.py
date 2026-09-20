# PYGBENCH: python.unpacking
# CATEGORY: python
# TAGS: unpacking, assignment
# EXPECT: ddg:pair[0] -> left
# EXPECT: ddg:pair[1] -> right
# FORBID: ddg:pair[0] -> right

def swap(pair):
    left, right = pair
    return right, left
