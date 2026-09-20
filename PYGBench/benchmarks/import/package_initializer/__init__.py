# PYGBENCH: import.package_initializer
# CATEGORY: import
# TAGS: package, initializer
# EXPECT: call:run -> initialized
# FORBID: call:run -> decoy

def initialize():
    return True

initialized = initialize

def decoy():
    return False

def run():
    return initialized()
