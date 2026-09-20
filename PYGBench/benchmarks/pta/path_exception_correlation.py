# PYGBENCH: pta.path_exception_correlation
# CATEGORY: pta
# TAGS: path-sensitive, exception, correlation
# EXPECT: points-to:true return -> right
# EXPECT: points-to:false return -> left
# FORBID: points-to:true return -> left
# FORBID: points-to:false return -> right

class Marker(Exception):
    pass

def choose(flag, left, right):
    try:
        if flag:
            raise Marker()
        value = left
    except Marker:
        value = right
    if flag:
        return value
    return value
