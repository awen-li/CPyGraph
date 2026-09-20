# PYGBENCH: cg.deep_direct_chain
# CATEGORY: cg
# TAGS: direct-call, interprocedural, multi-hop
# EXPECT: call:run -> stage_one
# EXPECT: call:stage_one -> stage_two
# EXPECT: call:stage_two -> stage_three
# EXPECT: call:stage_three -> stage_four
# EXPECT: call:stage_four -> leaf
# FORBID: call:leaf -> stage_four
# FORBID: call:stage_four -> stage_three
# FORBID: call:stage_three -> stage_two
# FORBID: call:stage_two -> stage_one
# FORBID: call:stage_one -> run
# EXPECT_PATH: cg:run -> stage_one -> stage_two -> stage_three -> stage_four -> leaf
# FORBID_PATH: cg:leaf -> stage_four -> stage_three -> stage_two -> stage_one -> run

def leaf(value):
    return value


def stage_four(value):
    return leaf(value)


def stage_three(value):
    return stage_four(value)


def stage_two(value):
    return stage_three(value)


def stage_one(value):
    return stage_two(value)


def run(value):
    return stage_one(value)
