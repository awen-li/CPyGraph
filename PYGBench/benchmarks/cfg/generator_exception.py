# PYGBENCH: cfg.generator_exception
# CATEGORY: cfg
# TAGS: generator, exception-flow
# EXPECT: cfg:yield resume -> next yield
# EXPECT: cfg:generator close -> finally
# FORBID: cfg:generator close -> function exit bypassing finally

def generate(cleanup):
    try:
        yield 1
        yield 2
    finally:
        cleanup()
