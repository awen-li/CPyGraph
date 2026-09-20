# PYGBENCH: python.variadic_arguments
# CATEGORY: python
# TAGS: argument-binding, varargs, kwargs
# EXPECT: ddg:first and second -> positional tuple
# EXPECT: ddg:named -> keyword mapping
# FORBID: ddg:named -> positional tuple

def collect(*positional, **keywords):
    return positional, keywords

def run(first, second, named):
    return collect(first, second, value=named)
