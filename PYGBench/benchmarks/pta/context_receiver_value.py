# PYGBENCH: pta.context_receiver_value
# CATEGORY: pta
# TAGS: context-sensitive, receiver, bound-method, field-modeling
# EXPECT: points-to:left result -> Left instance
# EXPECT: points-to:right result -> Right instance
# FORBID: points-to:left result -> Right instance
# FORBID: points-to:right result -> Left instance

class Left:
    pass

class Right:
    pass

class Holder:
    def __init__(self, payload):
        self.payload = payload

    def read(self):
        return self.payload

def extract(holder):
    return holder.read()

def left():
    return extract(Holder(Left()))

def right():
    return extract(Holder(Right()))
