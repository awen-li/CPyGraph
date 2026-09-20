# PYGBENCH: python.instance_dict
# CATEGORY: python
# TAGS: dynamic-attribute, instance-dict
# EXPECT: points-to:item.__dict__[payload] -> value
# EXPECT: points-to:item.payload -> value
# FORBID: points-to:item.__dict__[other] -> value

class Item:
    pass

def run(value):
    item = Item()
    item.__dict__["payload"] = value
    return item.payload
