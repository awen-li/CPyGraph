#include "api/cg.h"
#include "api/pta.h"
#include "test_support.h"

int main() {
    constexpr cpygraph::ObjectId marshal_module = 100;
    constexpr cpygraph::ObjectId loads_function = 200;
    constexpr cpygraph::ObjectId dumps_function = 201;
    constexpr cpygraph::ObjectId package_module = 300;
    constexpr cpygraph::ObjectId child_module = 301;
    constexpr cpygraph::ObjectId entry_function = 302;
    constexpr cpygraph::ObjectId alternate_module = 400;
    constexpr cpygraph::ObjectId alternate_loads = 401;

    cpygraph::AndersenPointerAnalysis pta;
    cpygraph::ImportAliasModel imports(pta);
    imports.defineAttribute(marshal_module, "loads", loads_function);
    imports.defineAttribute(marshal_module, "dumps", dumps_function);

    // import marshal as m; loader = m.loads; alias = loader
    imports.bindModule(1, marshal_module);
    imports.loadAttribute(1, "loads", 2);
    imports.bindAlias(2, 3);

    // from marshal import loads as decode
    imports.bindFromImport(1, "loads", 4);

    // Ensure sibling module attributes remain distinct.
    imports.loadAttribute(1, "dumps", 5);

    // import package as p; entry = p.child.entry
    imports.defineAttribute(package_module, "child", child_module);
    imports.defineAttribute(child_module, "entry", entry_function);
    imports.bindModule(6, package_module);
    imports.loadAttribute(6, "child", 7);
    imports.loadAttribute(7, "entry", 8);

    // A same-named attribute on another module must remain a different object.
    imports.defineAttribute(alternate_module, "loads", alternate_loads);
    imports.bindModule(9, alternate_module);
    imports.loadAttribute(9, "loads", 10);

    pta.solve();
    require(pta.pointsTo(1).count(marshal_module) == 1,
            "import-as binding points to imported module object");
    require(pta.pointsTo(3).count(loads_function) == 1,
            "ordinary aliases preserve imported attribute target");
    require(pta.pointsTo(4).count(loads_function) == 1,
            "from-import-as binding points to selected attribute");
    require(pta.pointsTo(5).count(dumps_function) == 1 &&
            pta.pointsTo(5).count(loads_function) == 0,
            "module attributes are not conflated");
    require(pta.pointsTo(8).count(entry_function) == 1,
            "chained imported module attributes resolve transitively");
    require(pta.pointsTo(10).count(alternate_loads) == 1 &&
            pta.pointsTo(10).count(loads_function) == 0,
            "same-named attributes on different imported modules are isolated");

    cpygraph::cg::CallGraphBuilder call_graph(pta);
    call_graph.registerCallable(loads_function, {20, 30});
    call_graph.registerCallable(dumps_function, {21, 31});
    call_graph.registerCallable(entry_function, {22, 32});
    const auto graph = call_graph.build({{1, 10, 3}, {2, 10, 4}, {3, 10, 5},
                                         {4, 10, 8}, {5, 10, 99}});
    require(graph.targets(1).front().target->function == 20,
            "call graph resolves import alias callable");
    require(graph.targets(2).front().target->function == 20,
            "call graph resolves from-import alias callable");
    require(graph.targets(3).front().target->function == 21,
            "call graph resolves correct sibling module attribute");
    require(graph.targets(4).front().target->function == 22,
            "call graph resolves chained import attribute callable");
    require(!graph.targets(5).front().target.has_value(),
            "unresolved imported name remains an explicit unknown call target");
}
