# PYGBENCH: ddg.local_def_use
# CATEGORY: ddg
# TAGS: local, intraprocedural
# EXPECT: ddg:assignment value -> return value
# FORBID: ddg:parameter input -> unrelated constant
# EXPECT_PATH: ddg:parameter input -> load input -> assignment value -> load value -> return value
# FORBID_PATH: ddg:unrelated constant -> assignment value -> load value -> return value

def local(input_value):
    value = input_value
    return value
