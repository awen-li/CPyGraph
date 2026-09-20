# PYGBENCH: cfg.explicit_raise
# CATEGORY: cfg
# TAGS: exception-flow, exception, explicit-raise
# EXPECT: cfg:raise ValueError -> matching handler
# EXPECT: cfg:matching handler -> handled return
# FORBID: cfg:raise ValueError -> normal return

def run(value):
    try:
        if value:
            raise ValueError(value)
        return value
    except ValueError:
        return None
