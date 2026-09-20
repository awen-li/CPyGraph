# PYGBENCH: cg.receiver_context
# CATEGORY: cg
# TAGS: object-separation, context-sensitive
# EXPECT: call:dispatch@First receiver -> First.handle
# EXPECT: call:dispatch@Second receiver -> Second.handle
# FORBID: call:dispatch@First receiver -> Second.handle

class First:
    def handle(self):
        return 1

class Second:
    def handle(self):
        return 2

def dispatch(receiver):
    return receiver.handle()

def run():
    return dispatch(First()), dispatch(Second())
