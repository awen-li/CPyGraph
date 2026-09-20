# PYGBENCH: cg.mutual_recursion
# CATEGORY: cg
# TAGS: recursion, cycle
# EXPECT: call:is_even -> is_odd
# EXPECT: call:is_odd -> is_even
# FORBID: call:is_even -> decoy

def is_even(value):
    return True if value == 0 else is_odd(value - 1)

def is_odd(value):
    return False if value == 0 else is_even(value - 1)

def decoy():
    return None
