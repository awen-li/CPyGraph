# PYGBENCH: python.slots
# CATEGORY: python
# TAGS: class, slots, field-modeling
# EXPECT: points-to:item.value -> constructor value
# FORBID: points-to:item.missing -> constructor value

class Item:
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value

def run(value):
    return Item(value).value
