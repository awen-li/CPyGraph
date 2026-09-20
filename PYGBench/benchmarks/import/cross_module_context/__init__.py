# PYGBENCH: import.cross_module_context
# CATEGORY: import
# TAGS: cross-module, context-sensitive, factory
# EXPECT: points-to:left result -> model.Left instance
# EXPECT: points-to:right result -> model.Right instance
# FORBID: points-to:left result -> model.Right instance
# FORBID: points-to:right result -> model.Left instance

from .factory import create
from .model import Left, Right

def left():
    return create(Left)

def right():
    return create(Right)
