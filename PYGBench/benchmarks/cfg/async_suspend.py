# PYGBENCH: cfg.async_suspend
# CATEGORY: cfg
# TAGS: async, suspension
# EXPECT: cfg:await -> suspend
# EXPECT: cfg:resume -> return
# FORBID: cfg:await -> return without resume

async def load(source):
    return await source.read()
