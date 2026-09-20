# PYGBENCH: ddg.closure_def_use
# CATEGORY: ddg
# TAGS: closure, cell
# EXPECT: ddg:outer parameter -> inner return
# FORBID: ddg:inner function object -> inner return

def outer(value):
    def inner():
        return value
    return inner
