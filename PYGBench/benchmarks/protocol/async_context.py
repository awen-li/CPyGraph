# PYGBENCH: protocol.async_context
# CATEGORY: protocol
# TAGS: hidden-call, async, context-manager
# EXPECT: call:run -> Resource.__aenter__
# EXPECT: call:run -> Resource.__aexit__
# FORBID: call:run -> Resource.__enter__

class Resource:
    async def __aenter__(self):
        return self

    async def __aexit__(self, kind, value, traceback):
        return False

    def __enter__(self):
        return self

async def run():
    async with Resource() as resource:
        return resource
