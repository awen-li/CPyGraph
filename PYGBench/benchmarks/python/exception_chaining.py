# PYGBENCH: python.exception_chaining
# CATEGORY: python
# TAGS: exception, chaining
# EXPECT: cfg:ValueError handler -> RuntimeError raise
# EXPECT: ddg:ValueError instance -> RuntimeError cause
# FORBID: cfg:ValueError handler -> normal return

def run():
    try:
        raise ValueError()
    except ValueError as error:
        raise RuntimeError() from error
