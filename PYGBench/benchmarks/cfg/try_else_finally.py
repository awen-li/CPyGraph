# PYGBENCH: cfg.try_else_finally
# CATEGORY: cfg
# TAGS: exception-flow, cleanup
# EXPECT: cfg:try success -> else
# EXPECT: cfg:try failure -> except
# EXPECT: cfg:else and except -> finally
# FORBID: cfg:try body -> return bypassing finally
# EXPECT_PATH: cfg:try failure -> except -> finally -> return
# FORBID_PATH: cfg:try failure -> else -> finally -> return

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
