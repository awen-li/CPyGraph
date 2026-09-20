# PYGBENCH: python.property
# CATEGORY: python
# TAGS: descriptor, property
# EXPECT: call:run write -> Item.value.setter
# EXPECT: call:run read -> Item.value.getter
# FORBID: call:run -> Item.unused

class Item:
    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, new_value):
        self._value = new_value

    def unused(self):
        return None

def run(value):
    item = Item()
    item.value = value
    return item.value
