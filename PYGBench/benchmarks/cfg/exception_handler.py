# PYGBENCH: cfg.exception_handler
# CATEGORY: cfg
# TAGS: exception-flow
# EXPECT: cfg:division -> ZeroDivisionError handler
# EXPECT: cfg:division -> normal return
# FORBID: cfg:handler -> normal return

def guarded_divide(numerator, denominator):
    try:
        return numerator / denominator
    except ZeroDivisionError:
        return None
