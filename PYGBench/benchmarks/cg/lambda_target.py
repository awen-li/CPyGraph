# PYGBENCH: cg.lambda_target
# CATEGORY: cg
# TAGS: lambda, indirect-call
# EXPECT: call:run -> lambda@make
# EXPECT: call:run -> make
# FORBID: call:run -> decoy

def make():
    return lambda value: value

def decoy(value):
    return value

def run(value):
    function = make()
    return function(value)
