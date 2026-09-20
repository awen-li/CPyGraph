# PYGBENCH: protocol.async_iteration
# CATEGORY: protocol
# TAGS: hidden-call, async, iterator
# EXPECT: call:run -> Stream.__aiter__
# EXPECT: call:run -> Stream.__anext__
# FORBID: call:run -> Stream.__iter__

class Stream:
    def __aiter__(self):
        return self

    async def __anext__(self):
        raise StopAsyncIteration

    def __iter__(self):
        return iter(())

async def run():
    async for value in Stream():
        return value
