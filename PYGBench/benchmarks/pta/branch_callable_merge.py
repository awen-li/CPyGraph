# PYGBENCH: pta.branch_callable_merge
# CATEGORY: pta
# TAGS: flow-sensitive, callable
# EXPECT: points-to:choose_and_call target -> increment
# EXPECT: points-to:choose_and_call target -> decrement
# FORBID: points-to:choose_and_call target -> choose_and_call

def increment(value):
    return value + 1


def decrement(value):
    return value - 1


def choose_and_call(flag, value):
    target = increment
    if flag:
        target = decrement
    return target(value)
