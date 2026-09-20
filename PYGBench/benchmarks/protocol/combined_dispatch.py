# PYGBENCH: protocol.combined_dispatch
# CATEGORY: protocol
# TAGS: context, iterator, operator, subscription
# EXPECT: call:exercise_protocols -> Protocols.__enter__
# EXPECT: call:exercise_protocols -> Protocols.__exit__
# EXPECT: call:exercise_protocols -> Protocols.__add__
# FORBID: call:exercise_protocols -> unrelated user function

class Protocols:
    def __init__(self, value):
        self.value = value

    def __enter__(self):
        return self

    def __exit__(self, exception_type, exception, traceback):
        return False

    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration

    def __bool__(self):
        return True

    def __getitem__(self, key):
        return self.value

    def __setitem__(self, key, value):
        self.value = value

    def __contains__(self, value):
        return value == self.value

    def __add__(self, other):
        return self

    def __call__(self):
        return self.value


def exercise_protocols(value):
    with Protocols(value) as managed:
        if managed:
            managed[0] = managed[0]
        value in managed
        managed + managed
        managed()
        for _ in managed:
            pass
