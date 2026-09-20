# PYGBENCH: ddg.loop_carried
# CATEGORY: ddg
# TAGS: loop-carried
# EXPECT: ddg:total update -> next total update
# EXPECT: ddg:total initialization -> return total
# FORBID: ddg:values container -> returned total directly

def total(values):
    result = 0
    for value in values:
        result = result + value
    return result
