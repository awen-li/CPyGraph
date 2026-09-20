# PYGBENCH: protocol.containment
# CATEGORY: protocol
# TAGS: hidden-call, containment
# EXPECT: call:run -> Collection.__contains__
# FORBID: call:run -> Collection.__iter__

class Collection:
    def __contains__(self, value):
        return True

    def __iter__(self):
        return iter(())

def run(value):
    return value in Collection()
