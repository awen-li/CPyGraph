# PYGBENCH: python.assignment_expression
# CATEGORY: python
# TAGS: assignment-expression
# EXPECT: ddg:source return -> value
# EXPECT: ddg:value -> condition and return
# FORBID: ddg:source function object -> value

def run(source):
    if value := source():
        return value
    return None
