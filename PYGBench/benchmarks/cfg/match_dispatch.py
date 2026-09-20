# PYGBENCH: cfg.match_dispatch
# CATEGORY: cfg
# TAGS: flow-sensitive, pattern-matching
# EXPECT: cfg:match -> zero arm
# EXPECT: cfg:match -> sequence arm
# EXPECT: cfg:match -> default arm
# FORBID: cfg:zero arm -> sequence arm

def classify(value):
    match value:
        case 0:
            return "zero"
        case [first, second]:
            return first + second
        case _:
            return None
