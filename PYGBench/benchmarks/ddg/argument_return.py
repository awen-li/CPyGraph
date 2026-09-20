# PYGBENCH: ddg.argument_return
# CATEGORY: ddg
# TAGS: interprocedural, argument
# EXPECT: ddg:run parameter -> identity parameter
# EXPECT: ddg:identity return -> run return
# FORBID: ddg:identity function object -> run return

def identity(value):
    return value

def run(value):
    return identity(value)
