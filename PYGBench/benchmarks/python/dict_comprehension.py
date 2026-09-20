# PYGBENCH: python.dict_comprehension
# CATEGORY: python
# TAGS: comprehension, nested-code, container
# EXPECT: 3.10..3.11 | call:run -> dictcomp
# FORBID: 3.12..3.14 | call:run -> dictcomp
# EXPECT: ddg:values elements -> dict values
# FORBID: ddg:values container object -> each dict value

def run(values):
    return {index: value for index, value in enumerate(values)}
