# PYGBENCH: python.comprehension_scope
# CATEGORY: python
# TAGS: comprehension, nested-code
# EXPECT: 3.10..3.11 | call:run -> listcomp
# FORBID: 3.12..3.14 | call:run -> listcomp
# EXPECT: 3.10..3.11 | ddg:run values -> listcomp iteration
# EXPECT: 3.12..3.14 | ddg:run values -> run iteration
# FORBID: ddg:listcomp target value -> outer unrelated

def run(values):
    unrelated = None
    return [value for value in values if value]
