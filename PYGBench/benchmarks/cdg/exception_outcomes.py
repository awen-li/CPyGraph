# PYGBENCH: cdg.exception_outcomes
# CATEGORY: cdg
# TAGS: control-dependence, exception, handler, branch-outcome
# EXPECT: cdg-normal:parse protected call -> parse value assignment
# EXPECT: cdg-exception:parse protected call -> exception dispatch entry
# EXPECT: cdg-normal:parse value assignment -> parse normal return
# EXPECT: cdg-true:ValueError handler check -> ValueError handler return
# FORBID: cdg-normal:parse protected call -> exception dispatch entry
# FORBID: cdg-exception:parse protected call -> parse value assignment

def parse(source):
    try:
        value = source()
    except ValueError:
        return 0
    return value
