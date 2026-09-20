# PYGBENCH: ddg.subscript_flow
# CATEGORY: ddg
# TAGS: heap, subscription
# EXPECT: ddg:parameter value -> items key store
# EXPECT: ddg:items key store -> return subscription
# FORBID: ddg:key -> returned payload

def round_trip(key, value):
    items = {}
    items[key] = value
    return items[key]
