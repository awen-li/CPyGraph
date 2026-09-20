# PYGBENCH: python.decorator
# CATEGORY: python
# TAGS: decorator, closure
# EXPECT: call:module -> decorate
# EXPECT: call:run -> wrapper
# EXPECT: call:wrapper -> target
# FORBID: call:run -> target directly

def decorate(function):
    def wrapper(value):
        return function(value)
    return wrapper

@decorate
def target(value):
    return value

def run(value):
    return target(value)
