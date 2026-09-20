# PYGBENCH: protocol.iteration
# CATEGORY: protocol
# TAGS: hidden-call, iterator
# EXPECT: call:run -> Values.__iter__
# EXPECT: call:run -> Values.__next__
# FORBID: call:run -> Values.unused

class Values:
    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration

    def unused(self):
        return None

def run():
    for value in Values():
        return value
