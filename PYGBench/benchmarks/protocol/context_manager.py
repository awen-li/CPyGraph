# PYGBENCH: protocol.context_manager
# CATEGORY: protocol
# TAGS: hidden-call, context-manager
# EXPECT: call:run -> Resource.__enter__
# EXPECT: call:run -> Resource.__exit__
# FORBID: call:run -> Resource.close

class Resource:
    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        return False

    def close(self):
        return None

def run():
    with Resource() as resource:
        return resource
