# PYGBENCH: negative.unused_method
# CATEGORY: negative
# TAGS: precision, receiver
# EXPECT: call:run -> Active.work
# FORBID: call:run -> Inactive.work

class Active:
    def work(self):
        return True

class Inactive:
    def work(self):
        return False

def run():
    return Active().work()
