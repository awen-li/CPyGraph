# PYGBENCH: python.class_body_scope
# CATEGORY: python
# TAGS: class-body, lexical-scope
# EXPECT: ddg:module seed -> Example.value
# EXPECT: ddg:Example.value -> Example.method return
# FORBID: ddg:Example.method self -> Example.value definition

seed = 1

class Example:
    value = seed

    def method(self):
        return self.value
