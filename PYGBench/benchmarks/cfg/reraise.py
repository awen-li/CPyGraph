# PYGBENCH: cfg.reraise
# CATEGORY: cfg
# TAGS: exception-flow, exception, reraise
# EXPECT: cfg:protected call exception -> inner handler
# EXPECT: cfg:bare raise -> caller exceptional exit
# FORBID: cfg:bare raise -> normal return

def run(action, record):
    try:
        return action()
    except Exception as error:
        record(error)
        raise
