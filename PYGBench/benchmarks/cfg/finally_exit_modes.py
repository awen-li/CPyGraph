# PYGBENCH: cfg.finally_exit_modes
# CATEGORY: cfg
# TAGS: exception-flow, cleanup, return
# EXPECT: cfg:try return -> finally cleanup
# EXPECT: cfg:try exception -> finally cleanup
# EXPECT: cfg:try fallthrough -> finally cleanup
# FORBID: cfg:any protected exit -> function exit bypassing cleanup

def run(mode, cleanup):
    try:
        if mode:
            return mode
        value = 1
    finally:
        cleanup()
    return value
