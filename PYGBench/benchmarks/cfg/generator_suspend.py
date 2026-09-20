# PYGBENCH: cfg.generator_suspend
# CATEGORY: cfg
# TAGS: generator, suspension
# EXPECT: cfg:yield -> resume
# EXPECT: cfg:resume -> loop header
# FORBID: cfg:yield -> generator exit only

def generate(values):
    for value in values:
        yield value
