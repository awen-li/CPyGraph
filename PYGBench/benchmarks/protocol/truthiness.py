# PYGBENCH: protocol.truthiness
# CATEGORY: protocol
# TAGS: hidden-call, truthiness
# EXPECT: call:run -> Flag.__bool__
# FORBID: call:run -> Flag.__len__

class Flag:
    def __bool__(self):
        return True

    def __len__(self):
        return 1

def run():
    return 1 if Flag() else 0
