# PYGBENCH: pta.flow_sequential_rebinding
# CATEGORY: pta
# TAGS: flow-sensitive, strong-update, sequential-rebinding
# EXPECT: points-to:before -> Left instance
# EXPECT: points-to:after -> Right instance
# FORBID: points-to:before -> Right instance
# FORBID: points-to:after -> Left instance

class Left:
    pass

class Right:
    pass

def run():
    current = Left()
    before = current
    current = Right()
    after = current
    return before, after
