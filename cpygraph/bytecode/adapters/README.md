# CPython bytecode adapters

This directory separates the shared lifting engine from CPython-version
definitions.

Each supported version owns one subdirectory:

```text
python3xx/
  adapter.h    public adapter type
  adapter.cpp  opcode-to-semantic-IR table and version feature flags
  opcodes.h    generated raw CPython opcode numbers
  opcode_overrides.json  optional compatibility-only generator inputs
```

The directory name and class follow one convention: `python315` contains
`Python315Adapter` and reports version `3.15`. CMake discovers matching
directories and generates the factory registry, so production code outside
the new directory does not change.

Raw opcode identities and bytecode-layout differences must stay inside the
matching version directory. The shared `table_adapter.*` core implements
decoding behavior common to all versions. `binary_operations.h` and
`comparison_operations.h` define shared opcode-operand identities rather than
raw opcodes.

Python language behavior can also change between releases. The adapter must
normalize such differences into `SemanticInstruction` fields. Downstream
analysis consumes those semantic fields and must not branch on raw opcode
numbers or CPython versions. Add a semantic-IR distinction when two versions
have observably different behavior that cannot be represented by existing
fields.

The opcode generator discovers the same directories. To generate only a newly
added version without installing every historical interpreter, run:

```bash
python3 tools/generate_opcode_macros.py --only 315 \
    --python315 /path/to/python3.15
```
