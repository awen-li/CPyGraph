# Python language semantics

This directory is the single home for version-independent Python language
semantics used by analysis. Keeping operation recognition and fact-driven
refinement together makes Python-specific behavior visible and maintainable.

The files have distinct responsibilities:

- `protocol.*` maps semantic bytecode operations and builtins to compact,
  numeric protocol identities and candidate special methods.
- `language_features.*` uses points-to and program facts to refine those
  candidates according to Python's ordered dispatch and fallback rules.

Current rules cover:

- `__bool__` before the `__len__` truthiness fallback;
- direct, in-place, reflected, and rich-comparison dispatch;
- `NotImplemented`-controlled operator fallback;
- existing/missing attribute lookup through `__getattribute__`, descriptors,
  and `__getattr__`, including module attributes;
- intrinsic results of hidden calls such as package imports.

New Python-specific analysis behavior belongs here when it models semantics
after bytecode has been normalized. These rules are version-independent only
when the relevant Python behavior is unchanged across supported releases.

Version-specific language behavior belongs in the matching
`bytecode/adapters/python3xx` directory. The adapter represents the difference
in semantic IR; this directory then analyzes that IR without inspecting a raw
opcode or CPython version. Generic inclusion constraints remain in
`analysis/pta`, and graph construction remains in the graph-owning analysis
components.
