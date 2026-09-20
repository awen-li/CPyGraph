# PYGBENCH: cfg.attribute_exception
# CATEGORY: cfg
# TAGS: exception-flow, exception, attribute
# EXPECT: cfg:protected attribute load -> AttributeError handler
# EXPECT: cfg:protected attribute load -> normal return
# FORBID: cfg:AttributeError handler -> normal attribute return

def run(value):
    try:
        return value.payload
    except AttributeError:
        return None
