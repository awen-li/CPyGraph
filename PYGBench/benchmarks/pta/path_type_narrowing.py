# PYGBENCH: pta.path_type_narrowing
# CATEGORY: pta
# TAGS: path-sensitive, type-narrowing, field-modeling
# EXPECT: points-to:isinstance true value.payload -> WithPayload payload
# FORBID: points-to:isinstance true value.payload -> WithoutPayload instance

class WithPayload:
    def __init__(self, payload):
        self.payload = payload

class WithoutPayload:
    pass

def run(value):
    if isinstance(value, WithPayload):
        return value.payload
    return None
