# PYGBENCH: cg.direct
# CATEGORY: cg
# TAGS: direct-call
# EXPECT: call:caller -> target
# FORBID: call:caller -> decoy

def target():
    return 1

def decoy():
    return 2

def caller():
    return target()
