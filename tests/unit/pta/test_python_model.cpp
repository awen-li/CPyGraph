#include "api/cg.h"
#include "api/pta.h"
#include "test_support.h"

int main() {
    cpygraph::AndersenPointerAnalysis pta;
    const auto callback = pta.internField("callback");
    const auto cell_contents = pta.internField("cell_contents");
    const auto any_element = pta.internField("element[*]");
    const auto code_field = pta.internField("__code__");
    const auto self_field = pta.internField("__self__");
    const auto function_field = pta.internField("__func__");

    // Operand-stack duplication, stores, and CFG joins become inclusion copies.
    pta.addAddressOf(1, 100);
    pta.addCopy(1, 2);  // COPY/DUP or local store/load
    pta.addAddressOf(3, 101);
    pta.addCopy(2, 4);  // one predecessor
    pta.addCopy(3, 4);  // another predecessor

    // Heap objects retain distinct instance attributes; stores are weak because
    // Andersen is flow-insensitive.
    pta.addAddressOf(10, 1000);
    pta.addAddressOf(11, 1001);
    pta.addFieldAddress(1000, callback, 2000);
    pta.addFieldAddress(1001, callback, 2001);
    pta.addFieldLoad(10, callback, 12);
    pta.addFieldLoad(11, callback, 13);
    pta.addAddressOf(14, 2002);
    pta.addFieldStore(14, 10, callback);

    // Closure cells and containers are heap objects. Unknown-index accesses use
    // a summary field rather than conflating the container with its elements.
    pta.addAddressOf(20, 3000);
    pta.addFieldAddress(3000, cell_contents, 3001);
    pta.addFieldLoad(20, cell_contents, 21);
    pta.addAddressOf(30, 4000);
    pta.addFieldAddress(4000, any_element, 4001);
    pta.addFieldLoad(30, any_element, 31);

    // Function and code-object identities stay separate. A __code__ store is a
    // weak update so both historical and replacement bindings remain visible.
    pta.addAddressOf(40, 5000);
    pta.addFieldAddress(5000, code_field, 5001);
    pta.addAddressOf(41, 5002);
    pta.addFieldStore(41, 40, code_field);
    pta.addFieldLoad(40, code_field, 42);

    // Bound methods are distinct callable objects carrying __self__/__func__.
    pta.addAddressOf(50, 6000);
    pta.addFieldAddress(6000, self_field, 1000);
    pta.addFieldAddress(6000, function_field, 2000);

    pta.solve();
    require(pta.pointsTo(4).count(100) && pta.pointsTo(4).count(101),
            "CFG join unions stack/local aliases from both predecessors");
    require(pta.pointsTo(12).count(2000) && pta.pointsTo(12).count(2002),
            "flow-insensitive attribute reassignment is a weak update");
    require(pta.pointsTo(12).count(2001) == 0 && pta.pointsTo(13).count(2001),
            "instance fields remain allocation-site sensitive");
    require(pta.pointsTo(21).count(3001), "closure cell load resolves cell contents");
    require(pta.pointsTo(31).count(4001), "unknown container index resolves summary element");
    require(pta.pointsTo(42).count(5001) && pta.pointsTo(42).count(5002),
            "function __code__ retains original and replacement objects");

    cpygraph::cg::CallGraphBuilder calls(pta);
    calls.registerCallable(6000, {70, 71});
    const auto graph = calls.build({{1, 1, 50}});
    require(graph.targets(1).size() == 1 && graph.targets(1)[0].target.has_value(),
            "bound-method object resolves as a callable without merging identities");
}
