# PYGBENCH: cfg.context_suppression
# CATEGORY: cfg
# TAGS: exception-flow, context-manager, hidden-call
# EXPECT: cfg:with body exception -> Manager.__exit__
# EXPECT: cfg:Manager.__exit__ true -> after with
# FORBID: cfg:with body exception -> after with bypassing Manager.__exit__

class Manager:
    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        return True

def run(action):
    with Manager():
        action()
    return True
