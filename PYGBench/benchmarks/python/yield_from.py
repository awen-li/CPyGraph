# PYGBENCH: python.yield_from
# CATEGORY: python
# TAGS: generator, delegation
# EXPECT: cfg:delegate yield-from -> suspend and resume
# EXPECT: ddg:values elements -> yielded values
# FORBID: call:delegate -> unrelated

def unrelated():
    return None

def delegate(values):
    yield from values
