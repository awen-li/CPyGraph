# PYGBENCH: cfg.with_cleanup
# CATEGORY: cfg
# TAGS: exception-flow, context-manager
# EXPECT: cfg:with normal exit -> context cleanup
# EXPECT: cfg:with exceptional exit -> context cleanup
# FORBID: cfg:with body -> function exit bypassing cleanup

def managed(context, action):
    with context:
        return action()
