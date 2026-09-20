# PYGBENCH: cfg.loop_back_edge
# CATEGORY: cfg
# TAGS: flow-sensitive, loop
# EXPECT: cfg:while body -> condition
# EXPECT: cfg:condition -> loop exit
# FORBID: cfg:loop exit -> while body

def count(limit):
    value = 0
    while value < limit:
        value += 1
    return value
