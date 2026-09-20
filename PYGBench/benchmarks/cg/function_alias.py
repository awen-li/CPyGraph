# PYGBENCH: cg.function_alias
# CATEGORY: cg
# TAGS: indirect-call, alias
# EXPECT: call:caller -> target
# FORBID: call:caller -> decoy

def target():
    return True

def decoy():
    return False

def caller():
    alias = target
    return alias()
