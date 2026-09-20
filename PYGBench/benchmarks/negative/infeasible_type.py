# PYGBENCH: negative.infeasible_type
# CATEGORY: negative
# TAGS: precision, type-flow
# EXPECT: call:run -> Left.handle
# FORBID: call:run -> Right.handle

class Left:
    def handle(self):
        return 1

class Right:
    def handle(self):
        return 2

def run():
    receiver = Left()
    return receiver.handle()
