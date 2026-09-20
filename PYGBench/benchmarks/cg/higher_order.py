# PYGBENCH: cg.higher_order
# CATEGORY: cg
# TAGS: higher-order, interprocedural
# EXPECT: call:apply -> increment
# EXPECT: call:run -> apply
# FORBID: call:apply -> decrement
# EXPECT_PATH: cg:run -> apply -> increment
# FORBID_PATH: cg:run -> apply -> decrement

def increment(value):
    return value + 1

def decrement(value):
    return value - 1

def apply(function, value):
    return function(value)

def run(value):
    return apply(increment, value)
