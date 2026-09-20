# PYGBENCH: cg.super_dispatch
# CATEGORY: cg
# TAGS: inheritance, super
# EXPECT: call:Child.work -> Base.work
# EXPECT: call:run -> Child.work
# FORBID: call:Child.work -> Child.work

class Base:
    def work(self):
        return "base"

class Child(Base):
    def work(self):
        return super().work()

def run():
    return Child().work()
