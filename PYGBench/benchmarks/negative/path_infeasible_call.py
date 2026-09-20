# PYGBENCH: negative.path_infeasible_call
# CATEGORY: negative
# TAGS: path-sensitive, precision, infeasible-path
# EXPECT: call:feasible branch -> live
# FORBID: call:contradictory branch -> dead

def live():
    return True

def dead():
    return False

def run(flag):
    if flag and not flag:
        return dead()
    return live()
