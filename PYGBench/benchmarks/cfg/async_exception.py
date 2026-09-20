# PYGBENCH: cfg.async_exception
# CATEGORY: cfg
# TAGS: exception-flow, async, exception, suspension
# EXPECT: cfg:await exceptional resume -> RuntimeError handler
# EXPECT: cfg:await normal resume -> normal return
# FORBID: cfg:await exceptional resume -> normal return

async def run(source):
    try:
        return await source.read()
    except RuntimeError:
        return None
