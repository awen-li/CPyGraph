# C++ component APIs

Applications may include one component header or `cpygraph/api.h` for the
complete C++ API.

| Component | Public header | Construction | Primary query |
| --- | --- | --- | --- |
| Package input | `api/package.h` | compile, load, then `package::GraphBuilder` | `analyze()` for one coupled multi-product run; individual component methods remain available |
| Adapters | `api/adapters.h` | `bytecode::AdapterFactory::create(version)` | `version()`, parsing, stack/exception semantics |
| Argument binding | `api/argument_binding.h` | `bindArguments(...)` | numeric actual-to-formal relationships |
| CFG | `api/cfg.h` | `cfg::CFGBuilder::build(...)` | `blocks()`, `edges()`, `blockForInstruction()`, `blockAtOffset()` |
| CDG | `api/cdg.h` | `cdg::CDGBuilder::build(cfg)` | `postdominates()`, `immediatePostDominator()`, `controllers()`, `dependents()` |
| PTA | `api/pta.h` | `AndersenPointerAnalysis` | `pointsTo(value)`, `unknownObject()` |
| CG | `api/cg.h` | `cg::OnTheFlyCallGraphBuilder(pta).build(provider)` | `targets(callsite)`, `unresolvedGroup(id)` |
| DDG | `api/ddg.h` | `ddg::DDGBuilder::build(cfg)` | `nodes()`, `edges()`, `hasEdge(...)` |
| Visualization | `api/visualization.h` | `CFGDotGenerator`, `CDGDotGenerator`, `CallGraphDotGenerator`, `DDGDotGenerator` | `generate()`, `write(path)` |
| Python protocols | `api/python.h` | `python::protocolFamily(instruction)` | `mayInvokeUserCode(instruction)` |
| Profiling | `api/profile.h` | enabled by default; `profile::Scope` for custom regions | `report()`, `enable()`, `disable()` |

The owning builders and solvers validate reserved identities and malformed
inputs with exceptions. Returned graph collections are immutable views.
`OnTheFlyCallGraphBuilder::registerCallable` introduces a stable
object-to-code binding and rejects accidental conflicts. `assignCallable` is
reserved for bytecode-visible Python overwrite semantics, such as sequential
definitions of the same class method; it intentionally replaces the earlier
target before fixed-point construction.
Unresolved call targets use interned numeric group IDs with enum/bit-mask
descriptors, preserving protocol identities without strings in nodes or edges.
Package graph results expose `SoundnessCoverage`: `ConcreteOnly` means the
materialized relation is complete, `TypedUnresolved` restricts uncertainty to
a semantic feature, and `ConservativeTop` means every relation in the
consumer's candidate domain remains possible. These are compact may-analysis
summaries, not a license to discard unknown behavior.

Package input follows three explicit stages:

```cpp
cpygraph::package::PackageCompiler compiler;
auto compiled = compiler.compile("path/to/package");
auto loaded = cpygraph::package::PackageLoader().load(compiled);
auto analysis = cpygraph::package::PackageAnalyzer().analyze(loaded);
auto result = cpygraph::package::GraphBuilder().analyze(analysis);
const auto& cg = result.call_graph;
const auto& cfgs = result.control_flow;
const auto& cdgs = result.control_dependency;
const auto& ddg = result.data_dependency;
```

A native bytecode artifact uses the same downstream API without source
compilation:

```cpp
auto loaded = cpygraph::package::PackageLoader().loadBytecode(
    "path/to/module.cpython-314.pyc");
auto analysis = cpygraph::package::PackageAnalyzer().analyze(loaded);
auto result = cpygraph::package::GraphBuilder().analyze(analysis);
```

The artifact must match the CPython minor version linked into CPyGraph.
`loadBytecode` reads only the requested artifact; it does not discover or
compile an adjacent source file.

`analyze()` runs the on-the-fly PTA/CG fixed point once. It then builds the
CFG, CDG, and DDG products from the same prerequisite and can optionally retain
the numeric PTA observations. Use a component method when the client needs only
that one graph and wants to avoid retaining the other products.

Package observations expose exact module-object/name pairs, attribute-access
receiver/field/value identities, and item-load base/key/result identities.
Access names remain distinct from the name table for materialized heap-field
facts. These observations expose existing PTA state and do not add graph
facts. The package DDG result exposes remapped call, ordered collection, and
heap-access boundaries whose node IDs belong to the returned graph. Collection
boundaries preserve source stack order without changing DDG edges; maps retain
key/value alternation and positional collections retain element order. Clients
can therefore anchor callable, structured-metadata, and module-qualified
attribute models without reconstructing bytecode stack behavior.

Inspect `cg.points_to_coverage`, `cg.call_graph_coverage`, `cfgs.coverage`,
`cdgs.coverage`, and `ddg.coverage` before treating materialized edges as a
complete may-result.

The compiler recursively discovers Python files, supports conventional `src/`
layouts, and treats `src/` as the source root only when it contains Python
files. Native-only `src/` trees therefore do not hide Python code elsewhere in
a mixed-language package. The compiler owns its temporary `.pyc` files and uses the interpreter selected
when CPyGraph was built and rejects a different CPython minor version. The
loader preserves module and nested-code-object identities; the analyzer lifts
each native code object through the matching adapter and constructs its CFG.

The adapter API is the boundary for CPython-version behavior. CFG, CDG, PTA,
CG, and DDG clients should not branch on version-specific opcode numbers.
See `adapter-extension.md` for the minimal version-adapter contract and opcode
macro generation policy. See `python-semantics.md` for implicit Python
protocol coverage and its soundness fallback.

## PTA-first graph construction

Package graph requests run through `package::OnTheFlyCallGraphAnalyzer`. Module
bodies seed the analysis; nested function, method, lambda, and comprehension
bodies add their PTA constraints only after a resolved call edge makes them
reachable. Each edge stitches actual arguments, formal parameters, and return
values, then resumes the incremental PTA solver. Construction stops only when
both points-to facts and call edges reach a fixed point.

Andersen points-to sets retain at most 16 concrete alternatives after they
contain the symbolic unknown object. The unknown object is lattice top, so
this widening bounds redundant concrete witnesses without dropping possible
runtime values. Direct `AndersenPointerAnalysis` clients may choose another
positive bound through its constructor.

Nested functions remain independent code objects. Their ordered free-variable
slots bind to numeric cell objects stored on each concrete closure object.
Activating a closure supplies that object as hidden environment input, so
`nonlocal`, sibling closures, late binding, and multi-level capture forwarding
share the correct storage identity without putting strings in graph nodes or
edges.

## Function-level PTA sensitivity

Every existing package graph entry point accepts an optional final
`PTASensitivityConfiguration` parameter. Its three policy levels are:

- `Insensitive`: the default; all functions use flow-, context-, and
  path-insensitive PTA.
- `Selective`: only listed `CodeObjectId` values receive their configured
  `Flow`, `Context`, and/or `Path` attributes.
- `Complete`: every function receives all three attributes.

`Flow` preserves reaching local definitions by program point; globals and
heap fields retain conservative weak-update behavior across calls.
`Context` clones PTA constraints using a bounded two-site call string. `Path` partitions
acyclic conditional branches and implies `Flow`; cyclic branches are widened
to preserve termination. `maximum_path_variants` limits exponential
partitioning, and branches beyond the limit remain conservatively merged.
These choices alter PTA constraint identities and propagation only. CFG and
DDG construction are unchanged, while CG continues to consume the resulting
on-the-fly PTA facts.

The call graph exposes these values through compact `CallGraphNode` records:
`CallGraph::nodes()` enumerates nodes and `CallGraph::node(CodeObjectId)` looks
one up. Unknown targets increase the caller's out-degree without allocating a
synthetic string-bearing node. Degrees are graph metadata only. The current
core changes sensitivity exclusively through an explicit per-function
configuration and never promotes it during on-the-fly construction.

Context and path instances remain numeric. When package observations are
requested, `PTAValueObservation`, `PTAObjectOrigin`, and
`PackageCallSiteObservation` expose their `PTAContextId`; zero denotes the
insensitive context. Call-site observations also retain the call result value,
the aggregate callable operand, the exact pre-aggregation stack values that
flow into that operand, all normalized argument-value alternatives, and the
leading implicit-receiver count. The exact callee inputs preserve an attribute
producer even when nested argument calls occur before the enclosing call, so
clients need not reconstruct a version-specific operand stack.
`PackageCallableObjectObservation` associates each materialized callable object
with its function identity, body code object, defining instruction, and PTA
context. `PTAModuleObjectObservation` maps a numeric receiver object to its
exact Python module identity. `PTAAttributeAccessObservation` exposes the
numeric receiver and value roles of individual attribute loads and stores; its
numeric field ID maps to the separate read-only debug-name table when that
field has materialized facts.
`PackageCallActivationObservation`
identifies both the caller and activated-target contexts at a resolved semantic
call site.

Clients that need only selected graph products can pass a compatible
`PackageCallGraphResult` and its observation snapshot to the
`GraphBuilder::dataDependencyGraph` overload. It reuses the fixed point and
preserves the snapshot without constructing unrelated graph products.

`PackageEagerCodeExecutionObservation` additionally links a defining code
object and semantic instruction index to a code body that Python executes
while evaluating that definition, including class bodies.
`PackageImportRequestObservation` exposes absolute module requests, decoded
fromlists, and source contexts. Invalid relative requests retain their lexical
module name and relative level, while unreachable CFG instructions do not
produce observations. Consumers recover bytecode offsets from the package
semantic program and do not interpret version-specific opcodes.

CG returns that coupled result directly. CFG, CDG, and DDG tools also complete this
PTA prerequisite before returning their requested graph. PTA internally needs
the structural control-flow edges for stack-state propagation, but those CFGs
are not exposed as a tool result until the PTA phase completes.

## Control dependence and CFG mappings

Each `cfg::BasicBlock` owns its ordered `instruction_indices`. The inverse
mapping is stored once as a compact `BlockId` vector and exposed through
`instructionBlocks()` and O(1) `blockForInstruction()`; `blockAtOffset()` maps
an exact semantic bytecode boundary without exposing native opcode numbers.

`cdg::PostDominatorBuilder` creates one synthetic exit for multiple returns
and termination sinks, then retains only the immediate-postdominator tree.
`postdominates()` walks that tree on demand, keeping persistent storage linear
in the number of CFG blocks. An immediate postdominator of zero denotes the
synthetic exit.

`cdg::CDGBuilder` applies the postdominator-frontier construction to CFG
blocks. A CDG edge contains only numeric controller/dependent block IDs and a
`BranchOutcome` enum: `True`, `False`, `Normal`, `Exception`, or `Resume`.
This distinguishes ordinary and exceptional decisions without storing strings
in graph nodes or edges.

## Profiling

The global profiler is enabled by default and package operations automatically
record their predefined `profile::Phase`. `profile::report()` returns both
individual records and fixed-size phase aggregates containing wall time,
process CPU time, and process RSS measurements. A lazy sampler observes RSS at
5 ms intervals while regions are active. `profile::disable()` removes the
sampling overhead, and `profile::enable()` resets and resumes collection.

Nodes and edges are unaffected: profiling phases, statuses, and tokens are
numeric types. String names are produced only by report-formatting code.
