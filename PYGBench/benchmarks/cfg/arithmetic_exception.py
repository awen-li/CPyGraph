# PYGBENCH: cfg.arithmetic_exception
# CATEGORY: cfg
# TAGS: exception-flow, exception, operator
# EXPECT: cfg:protected division -> ZeroDivisionError handler
# EXPECT: cfg:protected division -> normal return
# FORBID: cfg:ZeroDivisionError handler -> normal division return

def run(left, right):
    try:
        return left / right
    except ZeroDivisionError:
        return None
