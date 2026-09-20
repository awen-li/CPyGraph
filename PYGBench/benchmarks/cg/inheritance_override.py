# PYGBENCH: cg.inheritance_override
# CATEGORY: cg
# TAGS: inheritance, receiver-dispatch
# EXPECT: call:run child.work -> Child.work
# EXPECT: call:run base.work -> Base.work
# FORBID: call:run child.work -> Base.work

class Base:
    def work(self):
        return "base"

class Child(Base):
    def work(self):
        return "child"

def run():
    return Child().work(), Base().work()
