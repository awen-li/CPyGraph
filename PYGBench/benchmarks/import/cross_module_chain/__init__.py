# PYGBENCH: import.cross_module_chain
# CATEGORY: import
# TAGS: cross-module, call-chain
# EXPECT: call:run -> middle.forward
# EXPECT: call:middle.forward -> leaf.target
# FORBID: call:run -> leaf.decoy

from .middle import forward

def run(value):
    return forward(value)
