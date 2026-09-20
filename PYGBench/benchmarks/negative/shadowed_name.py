# PYGBENCH: negative.shadowed_name
# CATEGORY: negative
# TAGS: precision, lexical-scope
# EXPECT: call:run -> local target
# FORBID: call:run -> module target

def target():
    return "module"

def run():
    def target():
        return "local"
    return target()
