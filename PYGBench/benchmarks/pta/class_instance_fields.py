# PYGBENCH: pta.class_instance_fields
# CATEGORY: pta
# TAGS: field-modeling, class-attribute
# EXPECT: points-to:Item.shared -> class_value
# EXPECT: points-to:item.shared -> instance_value
# FORBID: points-to:Item.shared -> instance_value

class Item:
    shared = None

def separate(class_value, instance_value):
    Item.shared = class_value
    item = Item()
    item.shared = instance_value
    return Item.shared, item.shared
