# PYGBENCH: pta.context_factory
# CATEGORY: pta
# TAGS: context-sensitive, factory
# EXPECT: points-to:first caller result -> Alpha instance
# EXPECT: points-to:second caller result -> Beta instance
# FORBID: points-to:first caller result -> Beta instance

class Alpha:
    pass

class Beta:
    pass

def create(constructor):
    return constructor()

def first():
    return create(Alpha)

def second():
    return create(Beta)
