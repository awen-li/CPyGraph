# PYGBENCH: python.positional_only
# CATEGORY: python
# TAGS: argument-binding, positional-only
# EXPECT: ddg:run first -> combine left
# EXPECT: ddg:run second -> combine right
# FORBID: ddg:run second -> combine left

def combine(left, /, right):
    return left, right

def run(first, second):
    return combine(first, second)
