#pragma once

#include "analysis/pta/pointer_analysis.h"

#include <cstddef>
#include <deque>
#include <unordered_map>
#include <vector>

namespace cpygraph {

// Flow-insensitive, context-insensitive inclusion-based points-to analysis.
class AndersenPointerAnalysis final : public PointerAnalysis {
public:
    static constexpr std::size_t kDefaultMaximumConcreteAlternatives = 16U;

    explicit AndersenPointerAnalysis(
        std::size_t maximum_concrete_alternatives =
            kDefaultMaximumConcreteAlternatives);

    struct ContentFactView {
        ObjectId base{};
        ObjectId object{};
    };
    struct FieldFactView {
        ObjectId base{};
        FieldId field{};
        ObjectId object{};
    };
    struct Statistics {
        std::size_t value_count{};
        std::size_t object_count{};
        std::size_t constraint_count{};
        std::size_t points_to_fact_count{};
        std::size_t content_fact_count{};
        std::size_t field_fact_count{};
        std::size_t solver_iteration_count{};
    };

    NodeId newValue() override;
    ObjectId newObject() override;
    void addAddressOf(NodeId value, ObjectId object) override;
    void addCopy(NodeId source, NodeId target) override;
    void addLoad(NodeId pointer, NodeId target) override;
    void addStore(NodeId source, NodeId pointer) override;
    FieldId internField(std::string_view field) override;
    FieldId internIndexField(std::int64_t index) override;
    void bindFieldKey(ObjectId object, FieldId field) override;
    std::string_view debugField(FieldId field) const noexcept override;
    FieldId findField(std::string_view field) const;
    void addFieldAddress(ObjectId base, FieldId field, ObjectId object) override;
    void addFieldLoad(NodeId base, FieldId field, NodeId target) override;
    void addKeyedFieldLoad(NodeId base, NodeId key, NodeId target) override;
    void addFieldStore(NodeId source, NodeId base, FieldId field) override;
    void addUnknown(NodeId value) override;
    ObjectId unknownObject() const noexcept override;
    void solve() override;
    const PointsToSet& pointsTo(NodeId value) const override;
    std::vector<ContentFactView> contentFacts() const;
    std::vector<FieldFactView> fieldFacts() const;
    Statistics statistics() const noexcept;

private:
    void useValue(NodeId value);
    void useObject(ObjectId object);

    struct Constraint {
        NodeId first;
        NodeId second;
        bool operator==(const Constraint& other) const noexcept {
            return first == other.first && second == other.second;
        }
    };
    struct ConstraintHash {
        std::size_t operator()(const Constraint& constraint) const noexcept;
    };
    struct FieldLocation {
        ObjectId base;
        FieldId field;
        bool operator==(const FieldLocation& other) const noexcept {
            return base == other.base && field == other.field;
        }
    };
    struct FieldLocationHash {
        std::size_t operator()(const FieldLocation& location) const noexcept;
    };
    struct FieldConstraint {
        NodeId value;
        FieldId field;
        NodeId target;
        bool operator==(const FieldConstraint& other) const noexcept {
            return value == other.value && field == other.field && target == other.target;
        }
    };
    struct FieldConstraintHash {
        std::size_t operator()(const FieldConstraint& constraint) const noexcept;
    };
    struct KeyedFieldConstraint {
        NodeId base;
        NodeId key;
        NodeId target;
    };
    using TargetSet = std::unordered_set<NodeId>;
    using ValueFact = std::pair<NodeId, ObjectId>;
    using ContentFact = std::pair<ObjectId, ObjectId>;
    struct FieldFact { FieldLocation location; ObjectId object; };

    bool addValueFact(NodeId value, ObjectId object);
    void addContentFact(ObjectId base, ObjectId object);
    bool addFieldFact(FieldLocation location, ObjectId object);
    void registerContentLoad(ObjectId base, NodeId target);
    void registerFieldLoad(ObjectId base, const FieldConstraint& load);

    std::unordered_map<NodeId, PointsToSet> points_to_;
    std::unordered_set<NodeId> used_values_;
    std::unordered_set<ObjectId> used_objects_;
    NodeId next_value_{1};
    ObjectId next_object_{1};
    std::unordered_map<ObjectId, PointsToSet> object_contents_;
    std::vector<Constraint> copies_;
    std::vector<Constraint> loads_;
    std::vector<Constraint> stores_;
    std::unordered_set<Constraint, ConstraintHash> copy_keys_;
    std::unordered_set<Constraint, ConstraintHash> load_keys_;
    std::unordered_set<Constraint, ConstraintHash> store_keys_;
    std::unordered_map<FieldLocation, PointsToSet, FieldLocationHash> fields_;
    std::vector<FieldConstraint> field_loads_;
    std::vector<FieldConstraint> field_stores_;
    std::vector<KeyedFieldConstraint> keyed_field_loads_;
    std::unordered_set<FieldConstraint, FieldConstraintHash> field_load_keys_;
    std::unordered_set<FieldConstraint, FieldConstraintHash> field_store_keys_;

    std::size_t indexed_copies_{};
    std::size_t indexed_loads_{};
    std::size_t indexed_stores_{};
    std::size_t indexed_field_loads_{};
    std::size_t indexed_field_stores_{};
    std::unordered_map<NodeId, std::vector<NodeId>> copy_targets_;
    std::unordered_map<NodeId, std::vector<Constraint>> loads_by_pointer_;
    std::unordered_map<NodeId, std::vector<Constraint>> stores_by_source_;
    std::unordered_map<NodeId, std::vector<Constraint>> stores_by_pointer_;
    std::unordered_map<NodeId, std::vector<FieldConstraint>> field_loads_by_base_;
    std::unordered_map<NodeId, std::vector<FieldConstraint>> field_stores_by_source_;
    std::unordered_map<NodeId, std::vector<FieldConstraint>> field_stores_by_base_;
    std::unordered_map<NodeId, std::vector<std::size_t>> keyed_loads_by_key_;
    std::vector<std::unordered_set<FieldId>> keyed_load_fields_;
    std::unordered_map<ObjectId, FieldId> fields_by_key_object_;
    std::unordered_map<std::int64_t, FieldId> index_fields_;

    std::unordered_map<ObjectId, TargetSet> content_targets_;
    TargetSet all_content_targets_;
    std::unordered_map<FieldLocation, TargetSet, FieldLocationHash> exact_field_targets_;
    std::unordered_map<ObjectId, TargetSet> any_field_targets_;
    std::unordered_map<ObjectId, TargetSet> field_targets_by_base_;
    std::unordered_map<FieldId, TargetSet> field_targets_by_field_;
    TargetSet all_any_field_targets_;
    TargetSet all_field_targets_;
    std::unordered_map<ObjectId, std::unordered_set<FieldId>> fields_by_base_;

    std::deque<ValueFact> value_worklist_;
    std::deque<ContentFact> content_worklist_;
    std::deque<FieldFact> field_worklist_;
    std::size_t maximum_concrete_alternatives_;
    std::size_t address_constraint_count_{};
    std::size_t field_address_constraint_count_{};
    std::size_t solver_iteration_count_{};
    DebugStringTable field_names_;
    static const PointsToSet empty_;
};

}  // namespace cpygraph
