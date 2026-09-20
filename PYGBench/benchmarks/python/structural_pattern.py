# PYGBENCH: python.structural_pattern
# CATEGORY: python
# TAGS: pattern-matching, class
# EXPECT: cfg:Point pattern success -> coordinate return
# EXPECT: cfg:Point pattern failure -> default return
# FORBID: ddg:unmatched object -> coordinate return

class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y

def extract(value):
    match value:
        case Point(x, y):
            return x, y
        case _:
            return None
