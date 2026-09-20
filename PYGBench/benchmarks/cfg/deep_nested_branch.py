# PYGBENCH: cfg.deep_nested_branch
# CATEGORY: cfg
# TAGS: flow-sensitive, branch, multi-hop
# EXPECT: cfg:run entry -> first condition
# EXPECT: cfg:first condition -> first condition true outcome
# EXPECT: cfg:first condition true outcome -> second condition
# EXPECT: cfg:second condition -> second condition true outcome
# EXPECT: cfg:second condition true outcome -> third condition
# EXPECT: cfg:third condition -> true return
# EXPECT: cfg:second condition -> second condition false outcome
# EXPECT: cfg:third condition -> third condition false outcome
# FORBID: cfg:true return -> third condition
# FORBID: cfg:third condition -> second condition true outcome
# FORBID: cfg:second condition true outcome -> second condition
# FORBID: cfg:second condition -> first condition true outcome
# FORBID: cfg:first condition true outcome -> first condition
# FORBID: cfg:first condition -> run entry
# EXPECT_PATH: cfg:run entry -> first condition -> first condition true outcome -> second condition -> second condition true outcome -> third condition -> true return
# FORBID_PATH: cfg:true return -> third condition -> second condition true outcome -> second condition -> first condition true outcome -> first condition -> run entry

def run(first, second, third):
    if first:
        if second:
            if third:
                return 1
            return 2
        return 3
    return 4
