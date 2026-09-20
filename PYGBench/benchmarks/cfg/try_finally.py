# PYGBENCH: cfg.try_finally
# CATEGORY: cfg
# TAGS: exception-flow, cleanup
# EXPECT: cfg:normal try exit -> finally
# EXPECT: cfg:exceptional try exit -> finally
# FORBID: cfg:try body -> function exit bypassing finally

def guarded(resource, action):
    try:
        return action()
    finally:
        resource.close()
