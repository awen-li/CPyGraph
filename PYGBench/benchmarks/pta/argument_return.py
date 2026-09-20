# PYGBENCH: pta.argument_return
# CATEGORY: pta
# TAGS: interprocedural, return
# EXPECT: points-to:run result -> allocated Token
# FORBID: points-to:run result -> identity function

class Token:
    pass

def identity(value):
    return value

def run():
    return identity(Token())
