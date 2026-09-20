# PYGBENCH: ddg.deep_local_chain
# CATEGORY: ddg
# TAGS: local, intraprocedural, multi-hop
# EXPECT: ddg:parameter input -> load input
# EXPECT: ddg:load input -> assignment first
# EXPECT: ddg:assignment first -> load first
# EXPECT: ddg:load first -> assignment second
# EXPECT: ddg:assignment second -> return second
# FORBID: ddg:return second -> assignment second
# FORBID: ddg:assignment second -> load first
# FORBID: ddg:load first -> assignment first
# FORBID: ddg:assignment first -> load input
# FORBID: ddg:load input -> parameter input
# EXPECT_PATH: ddg:parameter input -> load input -> assignment first -> load first -> assignment second -> return second
# FORBID_PATH: ddg:return second -> assignment second -> load first -> assignment first -> load input -> parameter input

def run(input_value):
    first = input_value
    second = first
    return second
