# PYGBENCH: cg.method_callback
# CATEGORY: cg
# TAGS: context-sensitive, bound-method, higher-order
# EXPECT: call:invoke@First.work -> First.work
# EXPECT: call:invoke@Second.work -> Second.work
# FORBID: call:invoke@First.work -> Second.work

class First:
    def work(self):
        return 1

class Second:
    def work(self):
        return 2

def invoke(callback):
    return callback()

def run():
    return invoke(First().work), invoke(Second().work)
