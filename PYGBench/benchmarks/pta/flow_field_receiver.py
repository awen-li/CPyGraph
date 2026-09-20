# PYGBENCH: pta.flow_field_receiver
# CATEGORY: pta
# TAGS: flow-sensitive, field-modeling, receiver-rebinding
# EXPECT: points-to:first_box.payload -> Left instance
# EXPECT: points-to:second_box.payload -> Right instance
# FORBID: points-to:first_box.payload -> Right instance
# FORBID: points-to:second_box.payload -> Left instance

class Box:
    pass

class Left:
    pass

class Right:
    pass

def run():
    first_box = Box()
    second_box = Box()
    receiver = first_box
    receiver.payload = Left()
    receiver = second_box
    receiver.payload = Right()
    return first_box.payload, second_box.payload
