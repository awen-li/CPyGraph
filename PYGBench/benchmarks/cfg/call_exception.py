# PYGBENCH: cfg.call_exception
# CATEGORY: cfg
# TAGS: exception-flow, exception, call
# EXPECT: cfg:protected action call -> RuntimeError handler
# EXPECT: cfg:protected action call -> normal continuation
# FORBID: cfg:RuntimeError handler -> normal continuation

def run(action):
    try:
        value = action()
    except RuntimeError:
        return None
    return value
