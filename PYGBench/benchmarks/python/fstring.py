# PYGBENCH: python.fstring
# CATEGORY: python
# TAGS: formatting, hidden-call
# FORBID: call:run -> unrelated

def unrelated():
    return None

def run(value):
    return f"value={value!r:>10}"
