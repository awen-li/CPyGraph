# PYGBENCH: cfg.handler_selection
# CATEGORY: cfg
# TAGS: exception-flow, exception, handler
# EXPECT: cfg:ValueError exception -> ValueError handler
# EXPECT: cfg:TypeError exception -> TypeError handler
# EXPECT: cfg:other exception -> Exception handler
# FORBID: cfg:ValueError exception -> TypeError handler

def run(action):
    try:
        return action()
    except ValueError:
        return "value"
    except TypeError:
        return "type"
    except Exception:
        return "other"
