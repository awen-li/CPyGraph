# PYGBENCH: negative.overwritten_callable
# CATEGORY: negative
# TAGS: precision, strong-update
# EXPECT: call:run -> replacement
# FORBID: call:run -> original

def original():
    return 1

def replacement():
    return 2

def run():
    target = original
    target = replacement
    return target()
