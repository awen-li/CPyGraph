# PYGBENCH: ddg.exception_value
# CATEGORY: ddg
# TAGS: exception-flow, handler
# EXPECT: ddg:raised ValueError -> handler exception
# EXPECT: ddg:handler exception -> return
# FORBID: ddg:normal return value -> handler exception

def capture(value):
    try:
        raise ValueError(value)
    except ValueError as error:
        return error
