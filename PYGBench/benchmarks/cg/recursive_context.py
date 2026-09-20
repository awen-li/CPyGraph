# PYGBENCH: cg.recursive_context
# CATEGORY: cg
# TAGS: context-sensitive, recursion, higher-order
# EXPECT: call:repeat base -> callback
# EXPECT: call:repeat recursive -> repeat
# FORBID: call:repeat -> decoy

def target():
    return True

def decoy():
    return False

def repeat(count, callback):
    if count == 0:
        return callback()
    return repeat(count - 1, callback)

def run(count):
    return repeat(count, target)
