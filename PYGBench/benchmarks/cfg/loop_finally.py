# PYGBENCH: cfg.loop_finally
# CATEGORY: cfg
# TAGS: exception-flow, cleanup, loop
# EXPECT: cfg:continue -> finally cleanup
# EXPECT: cfg:break -> finally cleanup
# EXPECT: cfg:loop body exception -> finally cleanup
# FORBID: cfg:continue or break -> loop target bypassing cleanup

def run(values, cleanup):
    for value in values:
        try:
            if value < 0:
                continue
            if value == 0:
                break
        finally:
            cleanup(value)
