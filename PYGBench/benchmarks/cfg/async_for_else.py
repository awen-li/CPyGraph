# PYGBENCH: cfg.async_for_else
# CATEGORY: cfg
# TAGS: async, loop, suspension
# EXPECT: cfg:async next -> loop body
# EXPECT: cfg:async exhaustion -> else
# FORBID: cfg:break -> else

async def find(stream):
    async for value in stream:
        if value:
            break
    else:
        return None
    return value
