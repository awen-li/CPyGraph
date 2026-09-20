# PYGBENCH: cfg.break_continue
# CATEGORY: cfg
# TAGS: flow-sensitive, loop
# EXPECT: cfg:continue -> loop header
# EXPECT: cfg:break -> loop exit
# FORBID: cfg:break -> loop latch

def search(values):
    for value in values:
        if value < 0:
            continue
        if value == 0:
            break
    return value
