#include "api/pta.h"
#include "test_support.h"

#include <stdexcept>

int main() {
    cpygraph::AndersenPointerAnalysis analysis;
    analysis.addAddressOf(1, 101);
    analysis.addCopy(1, 2);
    analysis.addCopy(2, 3);
    analysis.addAddressOf(4, 301);
    analysis.addStore(3, 4);
    analysis.addLoad(4, 5);
    analysis.addAddressOf(6, 202);
    analysis.addStore(6, 4);
    analysis.solve();
    require(analysis.pointsTo(3).count(101) == 1, "Andersen reaches transitive copy fixed point");
    require(analysis.pointsTo(3).count(202) == 0,
            "indirect store does not collide object and value id namespaces");
    require(analysis.pointsTo(5).count(101) == 1 && analysis.pointsTo(5).count(202) == 1,
            "Andersen load observes all abstract pointee values");
    require(analysis.pointsTo(999).empty(), "unknown value has an explicit empty points-to set");
    const auto statistics = analysis.statistics();
    require(statistics.value_count == 6U &&
                statistics.object_count == 3U &&
                statistics.constraint_count == 8U &&
                statistics.points_to_fact_count == 7U &&
                statistics.content_fact_count == 2U &&
                statistics.solver_iteration_count == 1U,
            "Andersen exposes deterministic experiment cardinalities");

    cpygraph::AndersenPointerAnalysis cycle;
    cycle.addCopy(1, 2);
    cycle.addCopy(2, 1);
    cycle.addAddressOf(1, 77);
    cycle.solve();
    require(cycle.pointsTo(2).count(77) == 1, "Andersen terminates and propagates through copy cycles");

    cpygraph::AndersenPointerAnalysis fields;
    const auto value_field = fields.internField("value");
    fields.addAddressOf(10, 1000);
    fields.addFieldAddress(1000, value_field, 2000);
    fields.addFieldLoad(10, value_field, 11);
    fields.addAddressOf(12, 3000);
    fields.addFieldStore(12, 10, value_field);
    fields.solve();
    require(fields.pointsTo(11).count(2000) == 1 && fields.pointsTo(11).count(3000) == 1,
            "field loads and stores reach a joint fixed point");

    constexpr cpygraph::NodeId kIndexedBaseValue = 40U;
    constexpr cpygraph::NodeId kIndexedKeyValue = 41U;
    constexpr cpygraph::NodeId kIndexedResultValue = 42U;
    constexpr cpygraph::NodeId kCopiedKeyValue = 43U;
    constexpr cpygraph::ObjectId kIndexedBaseObject = 410U;
    constexpr cpygraph::ObjectId kIndexedKeyObject = 411U;
    constexpr cpygraph::ObjectId kIndexedExpectedObject = 412U;
    constexpr cpygraph::ObjectId kIndexedOtherObject = 413U;
    constexpr std::int64_t kSelectedIndex = 1;
    constexpr std::int64_t kOtherIndex = 2;
    cpygraph::AndersenPointerAnalysis indexed_fields;
    const auto selected_field = indexed_fields.internIndexField(kSelectedIndex);
    const auto other_field = indexed_fields.internIndexField(kOtherIndex);
    indexed_fields.bindFieldKey(kIndexedKeyObject, selected_field);
    indexed_fields.addAddressOf(kIndexedBaseValue, kIndexedBaseObject);
    indexed_fields.addFieldAddress(
        kIndexedBaseObject, selected_field, kIndexedExpectedObject);
    indexed_fields.addFieldAddress(
        kIndexedBaseObject, other_field, kIndexedOtherObject);
    indexed_fields.addKeyedFieldLoad(
        kIndexedBaseValue, kIndexedKeyValue, kIndexedResultValue);
    indexed_fields.addCopy(kIndexedKeyValue, kCopiedKeyValue);
    indexed_fields.solve();
    indexed_fields.addAddressOf(kIndexedKeyValue, kIndexedKeyObject);
    indexed_fields.solve();
    require(indexed_fields.pointsTo(kIndexedResultValue).count(
                kIndexedExpectedObject) == 1U &&
                indexed_fields.pointsTo(kIndexedResultValue).count(
                    kIndexedOtherObject) == 0U,
            "keyed field loads retain an exact numeric collection index");
    require(indexed_fields.pointsTo(kCopiedKeyValue).count(
                kIndexedKeyObject) == 1U,
            "key resolution does not interrupt other late value propagation");

    cpygraph::AndersenPointerAnalysis late_constraints;
    late_constraints.addAddressOf(20, 4000);
    late_constraints.solve();
    late_constraints.addCopy(20, 21);
    late_constraints.solve();
    require(late_constraints.pointsTo(21).count(4000) == 1,
            "solver accepts constraints discovered by later call-graph iterations");

    cpygraph::AndersenPointerAnalysis late_indirect;
    const auto late_field = late_indirect.internField("late");
    late_indirect.addAddressOf(1, 100);
    late_indirect.addAddressOf(2, 200);
    late_indirect.solve();
    late_indirect.addStore(2, 1);
    late_indirect.addLoad(1, 3);
    late_indirect.addFieldStore(2, 1, late_field);
    late_indirect.addFieldLoad(1, late_field, 4);
    late_indirect.solve();
    require(late_indirect.pointsTo(3).count(200) &&
            late_indirect.pointsTo(4).count(200),
            "incremental solver indexes indirect constraints added after a fixed point");
    late_indirect.addAddressOf(5, 300);
    late_indirect.addStore(5, 1);
    late_indirect.addFieldStore(5, 1, late_field);
    late_indirect.solve();
    require(late_indirect.pointsTo(3).count(300) &&
            late_indirect.pointsTo(4).count(300),
            "incremental solver propagates new facts through persistent watchers");

    cpygraph::AndersenPointerAnalysis unknown;
    unknown.addUnknown(30);
    unknown.addCopy(30, 31);
    unknown.addLoad(30, 32);
    unknown.addFieldLoad(30, unknown.internField("attribute"), 33);
    unknown.solve();
    require(unknown.pointsTo(31).count(unknown.unknownObject()) == 1 &&
            unknown.pointsTo(32).count(unknown.unknownObject()) == 1 &&
            unknown.pointsTo(33).count(unknown.unknownObject()) == 1,
            "explicit unknown object propagates through copy, load, and field load");

    cpygraph::AndersenPointerAnalysis collision;
    collision.addAddressOf(1, 2);
    collision.addAddressOf(2, 9000);
    collision.addLoad(1, 3);
    collision.solve();
    require(collision.pointsTo(3).count(9000) == 0,
            "object contents cannot alias an equal numeric value-node id");

    cpygraph::AndersenPointerAnalysis wildcard;
    const auto known_field = wildcard.internField("known");
    wildcard.addAddressOf(1, 100);
    wildcard.addAddressOf(2, 200);
    wildcard.addFieldStore(2, 1, 0);
    wildcard.addFieldLoad(1, known_field, 3);
    wildcard.solve();
    require(wildcard.pointsTo(3).count(200) == 1,
            "unknown-name field store reaches every specific field load");

    cpygraph::AndersenPointerAnalysis summaries;
    const auto left = summaries.internField("left");
    const auto right = summaries.internField("right");
    summaries.addAddressOf(1, 100);
    summaries.addFieldAddress(100, left, 201);
    summaries.addFieldAddress(100, right, 202);
    summaries.addFieldLoad(1, 0, 2);
    summaries.addUnknown(3);
    summaries.addAddressOf(4, 203);
    summaries.addFieldStore(4, 3, left);
    summaries.addFieldLoad(1, left, 5);
    summaries.addAddressOf(6, 204);
    summaries.addStore(6, 3);
    summaries.addLoad(1, 7);
    summaries.solve();
    require(summaries.pointsTo(2).count(201) && summaries.pointsTo(2).count(202),
            "unknown-name field load reads every field of possible base objects");
    require(summaries.pointsTo(5).count(summaries.unknownObject()) &&
            !summaries.pointsTo(5).count(203),
            "field store through an unresolved receiver remains symbolic top");
    require(summaries.pointsTo(7).count(summaries.unknownObject()) &&
            !summaries.pointsTo(7).count(204),
            "indirect store through an unresolved pointer remains symbolic top");

    cpygraph::AndersenPointerAnalysis mixed_unknown;
    const auto callback = mixed_unknown.internField("callback");
    mixed_unknown.addAddressOf(1, 100);
    mixed_unknown.addUnknown(2);
    mixed_unknown.addAddressOf(2, 200);
    mixed_unknown.addFieldStore(2, 1, callback);
    mixed_unknown.addFieldLoad(1, callback, 3);
    mixed_unknown.solve();
    require(mixed_unknown.pointsTo(3).count(mixed_unknown.unknownObject()) &&
            mixed_unknown.pointsTo(3).count(200),
            "unknown and concrete field alternatives coexist in the PTA lattice");

    cpygraph::AndersenPointerAnalysis unknown_reads;
    const auto member = unknown_reads.internField("member");
    unknown_reads.addAddressOf(1, 100);
    unknown_reads.addAddressOf(2, 200);
    unknown_reads.addStore(2, 1);
    unknown_reads.addUnknown(3);
    unknown_reads.addLoad(3, 4);
    unknown_reads.addFieldStore(2, 1, member);
    unknown_reads.addFieldLoad(3, member, 5);
    unknown_reads.solve();
    require(unknown_reads.pointsTo(4).count(unknown_reads.unknownObject()),
            "indirect load through unknown pointer preserves a symbolic top result");
    require(unknown_reads.pointsTo(5).count(unknown_reads.unknownObject()),
            "field load through unknown receiver preserves a symbolic top result");

    cpygraph::AndersenPointerAnalysis widened(2U);
    widened.addUnknown(1);
    widened.addAddressOf(1, 101);
    widened.addAddressOf(1, 102);
    widened.addAddressOf(1, 103);
    widened.solve();
    require(widened.pointsTo(1).count(widened.unknownObject()) &&
                widened.pointsTo(1).count(101) &&
                widened.pointsTo(1).count(102) &&
                !widened.pointsTo(1).count(103),
            "symbolic top bounds redundant concrete alternatives");

    cpygraph::AndersenPointerAnalysis identifiers;
    identifiers.addAddressOf(1, 1);
    require(identifiers.newValue() != 1 && identifiers.newObject() != 1,
            "PTA-generated identities do not collide with manually reserved identities");
    bool rejected_zero_value = false;
    try { identifiers.addCopy(0, 2); }
    catch (const std::invalid_argument&) { rejected_zero_value = true; }
    require(rejected_zero_value, "PTA rejects reserved value id zero");
    bool rejected_zero_object = false;
    try { identifiers.addAddressOf(3, 0); }
    catch (const std::invalid_argument&) { rejected_zero_object = true; }
    require(rejected_zero_object, "PTA rejects reserved object id zero");
    require(identifiers.newValue() == 3,
            "a rejected multi-id constraint does not partially reserve valid identities");
    bool rejected_zero_query = false;
    try { identifiers.pointsTo(0); }
    catch (const std::invalid_argument&) { rejected_zero_query = true; }
    require(rejected_zero_query, "PTA rejects queries for reserved value id zero");
}
