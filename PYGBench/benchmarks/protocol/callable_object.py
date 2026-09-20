# PYGBENCH: protocol.callable_object
# CATEGORY: protocol
# TAGS: hidden-call, callable-object
# EXPECT: call:run -> Handler.__call__
# FORBID: call:run -> Handler.unused

class Handler:
    def __call__(self, value):
        return value

    def unused(self):
        return None

def run(value):
    return Handler()(value)
