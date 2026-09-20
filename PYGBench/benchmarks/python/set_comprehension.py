# PYGBENCH: python.set_comprehension
# CATEGORY: python
# TAGS: comprehension, nested-code, container
# EXPECT: 3.10..3.11 | call:run -> setcomp
# FORBID: 3.12..3.14 | call:run -> setcomp
# EXPECT: ddg:values elements -> set elements
# FORBID: ddg:unrelated -> set elements

def run(values, unrelated):
    return {value for value in values}
