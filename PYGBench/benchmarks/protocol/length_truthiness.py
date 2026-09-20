# PYGBENCH: protocol.length_truthiness
# CATEGORY: protocol
# TAGS: hidden-call, truthiness, fallback
# EXPECT: call:run -> Collection.__len__
# FORBID: call:run -> Collection.unused

class Collection:
    def __len__(self):
        return 1

    def unused(self):
        return None

def run():
    return bool(Collection())
