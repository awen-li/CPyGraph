# PYGBENCH: python.function_attribute
# CATEGORY: python
# TAGS: function-object, field-modeling
# EXPECT: points-to:target.payload -> value
# FORBID: points-to:decoy.payload -> value

def target():
    return None

def decoy():
    return None

def run(value):
    target.payload = value
    return target.payload
