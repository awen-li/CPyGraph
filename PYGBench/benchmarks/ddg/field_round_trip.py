# PYGBENCH: ddg.field_round_trip
# CATEGORY: ddg
# TAGS: field-modeling, heap
# EXPECT: ddg:parameter value -> store box.value
# EXPECT: ddg:store box.value -> return load box.value
# FORBID: ddg:Box constructor -> returned payload

class Box:
    pass


def field_round_trip(value):
    box = Box()
    box.value = value
    return box.value
