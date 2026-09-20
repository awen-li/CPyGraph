# PYGBENCH: cg.two_level_context
# CATEGORY: cg
# TAGS: context-sensitive, two-call-site
# EXPECT: call:invoke@left_entry -> left
# EXPECT: call:invoke@right_entry -> right
# FORBID: call:invoke@left_entry -> right

def left():
    return 1

def right():
    return 2

def invoke(callback):
    return callback()

def forward(callback):
    return invoke(callback)

def left_entry():
    return forward(left)

def right_entry():
    return forward(right)
