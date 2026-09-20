# PYGBENCH: python.generator_expression
# CATEGORY: python
# TAGS: generator, nested-code
# EXPECT: call:run -> genexpr
# EXPECT: cfg:genexpr yield -> resume
# FORBID: call:run -> unrelated

def unrelated():
    return None

def run(values):
    return tuple(value for value in values)
