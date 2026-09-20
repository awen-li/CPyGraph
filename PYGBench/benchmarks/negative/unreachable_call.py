# PYGBENCH: negative.unreachable_call
# CATEGORY: negative
# TAGS: precision, unreachable
# EXPECT: call:run -> live
# FORBID: call:run -> dead

def live():
    return True

def dead():
    return False

def run():
    return live()
    dead()
