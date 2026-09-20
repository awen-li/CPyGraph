# PYGBENCH: pta.recursive_heap
# CATEGORY: pta
# TAGS: field-modeling, recursive-heap
# EXPECT: points-to:first.next -> second
# EXPECT: points-to:second.next -> first
# FORBID: points-to:first.next -> first

class Node:
    pass

def cycle():
    first = Node()
    second = Node()
    first.next = second
    second.next = first
    return first.next
