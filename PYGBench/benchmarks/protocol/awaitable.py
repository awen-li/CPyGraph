# PYGBENCH: protocol.awaitable
# CATEGORY: protocol
# TAGS: hidden-call, async, awaitable
# EXPECT: call:run -> Awaitable.__await__
# FORBID: call:run -> Awaitable.unused

class Awaitable:
    def __await__(self):
        yield None
        return 1

    def unused(self):
        return None

async def run():
    return await Awaitable()
