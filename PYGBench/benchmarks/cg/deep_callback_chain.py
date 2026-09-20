# PYGBENCH: cg.deep_callback_chain
# CATEGORY: cg
# TAGS: higher-order, indirect-call, interprocedural, multi-hop
# EXPECT: call:run -> dispatch
# EXPECT: call:dispatch -> apply
# EXPECT: call:apply -> invoke
# EXPECT: call:invoke -> callback
# EXPECT: call:callback -> leaf
# FORBID: call:leaf -> callback
# FORBID: call:callback -> invoke
# FORBID: call:invoke -> apply
# FORBID: call:apply -> dispatch
# FORBID: call:dispatch -> run
# EXPECT_PATH: cg:run -> dispatch -> apply -> invoke -> callback -> leaf
# FORBID_PATH: cg:leaf -> callback -> invoke -> apply -> dispatch -> run

def leaf(value):
    return value


def callback(value):
    return leaf(value)


def invoke(function, value):
    return function(value)


def apply(function, value):
    return invoke(function, value)


def dispatch(function, value):
    return apply(function, value)


def run(value):
    return dispatch(callback, value)
