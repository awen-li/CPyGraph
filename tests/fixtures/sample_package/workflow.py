from .helper import imported
import sample_package.helper as helper_alias


def identity(value):
    return value


def run(flag):
    if flag:
        return identity(flag)
    return identity(None)


def dormant():
    return identity("not reached")


run(True)
imported(True)
helper_alias.imported(False)
