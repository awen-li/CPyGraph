# Extending CPython version support

Version adapters are declarative. The shared `TableDrivenCPythonAdapter` owns:

- wordcode parsing and `EXTENDED_ARG` handling;
- cache omission;
- operand/name/local/constant lookup;
- absolute and relative jump calculation;
- fixed and dynamic stack effects;
- conditional edge stack effects;
- call protocol, keyword, and expanded-call handling;
- function-creation inputs;
- import metadata;
- paired-local decoding;
- encoded exception-table decoding.

A version adapter lives in `bytecode/adapters/python3xx/` and should contain
only:

1. `opcodes.h`, the generated native opcode definitions;
2. `adapter.cpp`, the opcode-to-semantics table and `AdapterFeatures` value;
3. `adapter.h`, the public adapter type;
4. a small override only for behavior unique to that release.

The directory and class names are registration metadata. For example,
`python315/adapter.h` declares `Python315Adapter`, whose `version()` returns
`3.15`. CMake discovers `python*/adapter.cpp`, compiles it, and generates the
factory entries. The public aggregate API exposes the factory rather than an
enumeration of concrete versions.

Raw opcode identities must use version-qualified macros such as
`CPYGRAPH_PY312_LOAD_GLOBAL`. Direct numeric opcode literals are not allowed in
adapter implementations. Each version's generated definitions live beside its
adapter and are produced from the selected runtime's `opcode.opmap` by:

```bash
python3 tools/generate_opcode_macros.py --only 315 \
    --python315 /path/to/python3.15
```

Compatibility-only opcodes absent from `opcode.opmap` must be named explicitly
in the version-local `opcode_overrides.json`; they must never masquerade as
official entries.

If Python language behavior differs between releases, normalize the difference
into semantic IR in the version adapter. Shared analyses must consume semantic
fields rather than test a CPython version or raw opcode.

## Additive annotation metadata

Annotation clients may inspect four observational fields on semantic
instructions: `setup_annotations`, `function_attribute_flags`,
`constant_string_tuple`, and `source_position`. These fields do not participate
in opcode normalization, stack effects, or graph construction.

- Map `SETUP_ANNOTATIONS` through `setupAnnotations()`. It remains the same
  zero-input, zero-output `Nop` used by existing analyses.
- Preserve raw `MAKE_FUNCTION` or `SET_FUNCTION_ATTRIBUTE` bits in
  `function_attribute_flags`; continue to apply the existing normalized
  operand and stack behavior.
- Copy all-string tuple constants and code-object position data when present.
  Missing metadata is represented by an empty tuple or unavailable position,
  not by a guessed value.

Adapter and package-loader tests must verify both the new metadata and the
unchanged semantic opcode and stack behavior.

No production source outside `bytecode/adapters/python3xx/` needs editing for a
new version. Conformance tests still belong under `tests/`; before accepting a
version, run the shared-core unit test, its version tests, and the complete test
suite.
