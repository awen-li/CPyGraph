# PYGBENCH: cfg.short_circuit
# CATEGORY: cfg
# TAGS: flow-sensitive, boolean
# EXPECT: cfg:left false -> return
# EXPECT: cfg:left true -> evaluate right
# FORBID: cfg:left false -> evaluate right
# EXPECT_PATH: cfg:left true -> evaluate right -> return
# FORBID_PATH: cfg:left false -> evaluate right -> return

def both(left, right):
    return left and right()
