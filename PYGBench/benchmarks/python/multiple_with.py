# PYGBENCH: python.multiple_with
# CATEGORY: python
# TAGS: context-manager, cleanup, nesting
# EXPECT: call:run -> Left.__enter__
# EXPECT: call:run -> Right.__enter__
# EXPECT: call:run cleanup -> Left.__exit__
# EXPECT: call:run cleanup -> Right.__exit__
# FORBID: call:run -> unused
# EXPECT: cfg:right exit -> left exit
# FORBID: cfg:left exit -> right exit

class Left:
    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        return False


class Right:
    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        return False


def unused():
    return None


def run():
    left = Left()
    right = Right()
    with left as first, right as second:
        return first, second
