# PYGBENCH: python.call_unpacking
# CATEGORY: python
# TAGS: argument-binding, unpacking
# EXPECT: ddg:positional elements -> target positional parameters
# EXPECT: ddg:keywords values -> target keyword parameters
# FORBID: ddg:keywords keys -> target data values

def target(first, second, option=None):
    return first, second, option

def run(positional, keywords):
    return target(*positional, **keywords)
