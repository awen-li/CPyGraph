# PYGBENCH: cg.bound_method
# CATEGORY: cg
# TAGS: method, receiver
# EXPECT: call:run -> Worker.execute
# FORBID: call:run -> Other.execute

class Worker:
    def execute(self):
        return True

class Other:
    def execute(self):
        return False

def run():
    method = Worker().execute
    return method()
