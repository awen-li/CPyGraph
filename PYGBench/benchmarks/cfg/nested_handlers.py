# PYGBENCH: cfg.nested_handlers
# CATEGORY: cfg
# TAGS: exception-flow, nested
# EXPECT: cfg:KeyError raise -> inner handler
# EXPECT: cfg:other exception -> outer handler
# FORBID: cfg:KeyError raise -> outer handler directly

def nested(mapping, key):
    try:
        try:
            return mapping[key]
        except KeyError:
            return None
    except Exception:
        return False
