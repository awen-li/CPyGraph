# PYGBENCH: cg.package_precision_recall
# CATEGORY: cg
# TAGS: package, flow-sensitive, protocol, negative
# EXPECT: call:run -> selected
# EXPECT: call:choose_and_call -> branch_left
# EXPECT: call:choose_and_call -> branch_right
# FORBID: call:run -> unreachable_decoy

def selected(value):
    return value


def branch_left(value):
    return value


def branch_right(value):
    return value


def unreachable_decoy(value):
    return value


class Context:
    def __init__(self):
        self.value = True

    def __enter__(self):
        return self.value

    def __exit__(self, exception_type, exception, traceback):
        return False


class Number:
    def __init__(self, value):
        self.value = value

    def __add__(self, other):
        return self.value

    def __sub__(self, other):
        return unreachable_decoy(other)


def choose_and_call(flag, value):
    if flag:
        target = branch_left
    else:
        target = branch_right
    return target(value)


def run(flag):
    with Context() as entered:
        number = Number(entered)
        selected(number + flag)
        return choose_and_call(flag, number)


run(True)
