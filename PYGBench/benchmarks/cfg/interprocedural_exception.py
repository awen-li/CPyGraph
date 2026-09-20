# PYGBENCH: cfg.interprocedural_exception
# CATEGORY: cfg
# TAGS: exception-flow, interprocedural, exception
# EXPECT: cfg:callee raise -> caller handler
# EXPECT: call:caller -> callee
# FORBID: cfg:callee raise -> caller normal continuation

def callee(value):
    if value:
        raise ValueError(value)
    return value

def caller(value):
    try:
        return callee(value)
    except ValueError:
        return None
