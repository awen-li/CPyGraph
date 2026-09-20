# PYGBENCH: pta.capture_rebinding
# CATEGORY: pta
# TAGS: closure, nonlocal, cell, flow-sensitive, nested-code, interprocedural
# EXPECT: points-to:invoke return -> replacement parameter value
# FORBID: points-to:invoke return -> initial parameter value

def bind(initial, replacement):
    callback = initial

    def update():
        nonlocal callback
        callback = replacement

    def invoke():
        return callback

    update()
    return invoke()
