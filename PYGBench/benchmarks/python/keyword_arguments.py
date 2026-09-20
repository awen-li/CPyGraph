# PYGBENCH: python.keyword_arguments
# CATEGORY: python
# TAGS: argument-binding, keyword
# EXPECT: ddg:run left -> combine parameter left
# EXPECT: ddg:run right -> combine parameter right
# FORBID: ddg:run left -> combine parameter right

def combine(left, right):
    return left, right

def run(first, second):
    return combine(right=second, left=first)
