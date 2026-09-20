# PYGBENCH: python.keyword_only
# CATEGORY: python
# TAGS: argument-binding, keyword-only
# EXPECT: ddg:run value -> target value
# EXPECT: ddg:run option -> target option
# FORBID: ddg:run value -> target option

def target(value, *, option):
    return value, option

def run(first, second):
    return target(first, option=second)
