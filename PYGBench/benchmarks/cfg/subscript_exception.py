# PYGBENCH: cfg.subscript_exception
# CATEGORY: cfg
# TAGS: exception-flow, exception, subscription
# EXPECT: cfg:protected subscript -> KeyError handler
# EXPECT: cfg:protected subscript -> IndexError handler
# FORBID: cfg:KeyError handler -> IndexError handler

def run(values, key):
    try:
        return values[key]
    except KeyError:
        return None
    except IndexError:
        return False
