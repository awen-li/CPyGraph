# PYGBENCH: cfg.deep_exception_cleanup
# CATEGORY: cfg
# TAGS: exception-flow, cleanup, multi-hop
# EXPECT: cfg:run entry -> protected action call
# EXPECT: cfg:protected action call -> ValueError handler
# EXPECT: cfg:ValueError handler -> finally cleanup
# EXPECT: cfg:finally cleanup -> normal return
# EXPECT: cfg:protected action call -> normal continuation
# EXPECT: cfg:normal continuation -> finally cleanup
# FORBID: cfg:normal return -> finally cleanup
# FORBID: cfg:finally cleanup -> ValueError handler
# FORBID: cfg:ValueError handler -> protected action call
# FORBID: cfg:protected action call -> run entry
# EXPECT_PATH: cfg:run entry -> protected action call -> ValueError handler -> finally cleanup -> normal return
# FORBID_PATH: cfg:normal return -> finally cleanup -> ValueError handler -> protected action call -> run entry

def run(action, cleanup):
    try:
        value = action()
    except ValueError:
        value = None
    else:
        value = value + 1
    finally:
        cleanup()
    return value
