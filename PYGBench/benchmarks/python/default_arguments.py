# PYGBENCH: python.default_arguments
# CATEGORY: python
# TAGS: argument-binding, defaults
# EXPECT: call:run first -> target with default callback
# EXPECT: call:target -> identity
# FORBID: call:target -> decoy

def identity(value):
    return value

def decoy(value):
    return value

def target(value, callback=identity):
    return callback(value)

def run(value):
    return target(value)
