# PYGBENCH: cfg.contradictory_guards
# CATEGORY: cfg
# TAGS: path-sensitive, infeasible-path
# EXPECT: cfg:entry -> first guard true and second guard false
# FORBID: cfg:first guard true -> second guard true return

def run(flag):
    if flag:
        if not flag:
            return "infeasible"
        return "feasible"
    return "false"
