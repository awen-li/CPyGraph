import sys

command_line_entry = sys.argv[0]

from .helpers import imported


arguments = sys.argv


class Resource:
    def __init__(self, value):
        self.value = value

    def __enter__(self):
        return self

    def __exit__(self, exception_type, exception, traceback):
        return False

    def __add__(self, other):
        return self.value


def exercise(value):
    with Resource(value) as resource:
        return imported(resource + value)


exercise(True)
