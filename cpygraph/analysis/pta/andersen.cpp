#include "analysis/pta/andersen.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace cpygraph {
namespace {

constexpr std::string_view kCollectionIndexFieldPrefix =
    "$cpygraph.collection.index.";

}  // namespace

const PointsToSet AndersenPointerAnalysis::empty_{};

AndersenPointerAnalysis::AndersenPointerAnalysis(
    std::size_t maximum_concrete_alternatives)
    : maximum_concrete_alternatives_(maximum_concrete_alternatives) {
    if (maximum_concrete_alternatives_ == 0U)
        throw std::invalid_argument(
            "PTA concrete-alternative budget must be positive");
}

void AndersenPointerAnalysis::useValue(NodeId value) {
    if (value == 0) throw std::invalid_argument("PTA value id zero is reserved");
    used_values_.insert(value);
}

void AndersenPointerAnalysis::useObject(ObjectId object) {
    if (object == 0) throw std::invalid_argument("PTA object id zero is reserved");
    used_objects_.insert(object);
}

NodeId AndersenPointerAnalysis::newValue() {
    while (next_value_ != 0 && used_values_.count(next_value_)) ++next_value_;
    if (next_value_ == 0) throw std::overflow_error("PTA value id space exhausted");
    const auto result = next_value_++;
    used_values_.insert(result);
    return result;
}

ObjectId AndersenPointerAnalysis::newObject() {
    while (next_object_ != 0 &&
           (next_object_ == unknownObject() || used_objects_.count(next_object_)))
        ++next_object_;
    if (next_object_ == 0 || next_object_ == unknownObject())
        throw std::overflow_error("PTA object id space exhausted");
    const auto result = next_object_++;
    used_objects_.insert(result);
    return result;
}

bool AndersenPointerAnalysis::addValueFact(NodeId value, ObjectId object) {
    auto& facts = points_to_[value];
    if (object != unknownObject() &&
        facts.size() > maximum_concrete_alternatives_ &&
        facts.count(unknownObject()) != 0U)
        return false;
    const auto inserted = facts.insert(object).second;
    if (inserted)
        value_worklist_.emplace_back(value, object);
    return inserted;
}

void AndersenPointerAnalysis::addContentFact(ObjectId base, ObjectId object) {
    if (base == unknownObject()) object = unknownObject();
    auto& facts = object_contents_[base];
    if (facts.insert(object).second) content_worklist_.emplace_back(base, object);
}

bool AndersenPointerAnalysis::addFieldFact(FieldLocation location, ObjectId object) {
    if (location.base == unknownObject()) object = unknownObject();
    auto& facts = fields_[location];
    if (!facts.insert(object).second) return false;
    fields_by_base_[location.base].insert(location.field);
    field_worklist_.push_back({location, object});
    return true;
}

void AndersenPointerAnalysis::addAddressOf(NodeId value, ObjectId object) {
    if (value == 0 || object == 0)
        throw std::invalid_argument("PTA address constraint contains a reserved id");
    useValue(value);
    useObject(object);
    if (addValueFact(value, object)) ++address_constraint_count_;
}

void AndersenPointerAnalysis::addCopy(NodeId source, NodeId target) {
    if (source == 0 || target == 0)
        throw std::invalid_argument("PTA copy constraint contains a reserved id");
    useValue(source);
    useValue(target);
    const Constraint constraint{source, target};
    if (copy_keys_.insert(constraint).second) copies_.push_back(constraint);
}

void AndersenPointerAnalysis::addLoad(NodeId pointer, NodeId target) {
    if (pointer == 0 || target == 0)
        throw std::invalid_argument("PTA load constraint contains a reserved id");
    useValue(pointer);
    useValue(target);
    const Constraint constraint{pointer, target};
    if (load_keys_.insert(constraint).second) loads_.push_back(constraint);
}

void AndersenPointerAnalysis::addStore(NodeId source, NodeId pointer) {
    if (source == 0 || pointer == 0)
        throw std::invalid_argument("PTA store constraint contains a reserved id");
    useValue(source);
    useValue(pointer);
    const Constraint constraint{source, pointer};
    if (store_keys_.insert(constraint).second) stores_.push_back(constraint);
}

std::size_t AndersenPointerAnalysis::ConstraintHash::operator()(
    const Constraint& constraint) const noexcept {
    const auto first = std::hash<NodeId>{}(constraint.first);
    const auto second = std::hash<NodeId>{}(constraint.second);
    return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
}

std::size_t AndersenPointerAnalysis::FieldLocationHash::operator()(
    const FieldLocation& location) const noexcept {
    const auto object = std::hash<ObjectId>{}(location.base);
    const auto field = std::hash<FieldId>{}(location.field);
    return object ^ (field + 0x9e3779b9U + (object << 6U) + (object >> 2U));
}

FieldId AndersenPointerAnalysis::internField(std::string_view field) {
#ifdef CPYGRAPH_ABLATE_FIELD_MODEL
    static_cast<void>(field);
    return FieldId{};
#else
    return field_names_.intern(std::string(field));
#endif
}

FieldId AndersenPointerAnalysis::internIndexField(std::int64_t index) {
    const auto found = index_fields_.find(index);
    if (found != index_fields_.end()) return found->second;
    const auto field = internField(
        std::string(kCollectionIndexFieldPrefix) + std::to_string(index));
    index_fields_.emplace(index, field);
    return field;
}

void AndersenPointerAnalysis::bindFieldKey(ObjectId object, FieldId field) {
    if (object == 0U || object == unknownObject())
        throw std::invalid_argument("PTA field-key object id is reserved");
    useObject(object);
    fields_by_key_object_.insert_or_assign(object, field);
}

std::string_view AndersenPointerAnalysis::debugField(FieldId field) const noexcept {
    return field_names_.lookup(field);
}

FieldId AndersenPointerAnalysis::findField(std::string_view field) const {
    return field_names_.find(field);
}

void AndersenPointerAnalysis::addFieldAddress(ObjectId base, FieldId field,
                                               ObjectId object) {
    if (base == 0 || object == 0)
        throw std::invalid_argument("PTA field address contains a reserved id");
    useObject(base);
    useObject(object);
    if (addFieldFact({base, field}, object))
        ++field_address_constraint_count_;
}

void AndersenPointerAnalysis::addFieldLoad(NodeId base, FieldId field, NodeId target) {
    if (base == 0 || target == 0)
        throw std::invalid_argument("PTA field load contains a reserved id");
    useValue(base);
    useValue(target);
    const FieldConstraint constraint{base, field, target};
    if (field_load_keys_.insert(constraint).second) field_loads_.push_back(constraint);
}

void AndersenPointerAnalysis::addKeyedFieldLoad(NodeId base, NodeId key,
                                                NodeId target) {
    if (base == 0U || key == 0U || target == 0U)
        throw std::invalid_argument(
            "PTA keyed field load contains a reserved id");
    useValue(base);
    useValue(key);
    useValue(target);
    const auto index = keyed_field_loads_.size();
    keyed_field_loads_.push_back({base, key, target});
    keyed_load_fields_.emplace_back();
    keyed_loads_by_key_[key].push_back(index);
    const auto keys = points_to_.find(key);
    if (keys == points_to_.end()) return;
    for (const auto object : keys->second) {
        const auto found = fields_by_key_object_.find(object);
        if (found == fields_by_key_object_.end()) continue;
        const auto field = found->second;
        if (keyed_load_fields_[index].insert(field).second)
            addFieldLoad(base, field, target);
    }
}

void AndersenPointerAnalysis::addFieldStore(NodeId source, NodeId base, FieldId field) {
    if (source == 0 || base == 0)
        throw std::invalid_argument("PTA field store contains a reserved id");
    useValue(source);
    useValue(base);
    const FieldConstraint constraint{source, field, base};
    if (field_store_keys_.insert(constraint).second) field_stores_.push_back(constraint);
}

std::size_t AndersenPointerAnalysis::FieldConstraintHash::operator()(
    const FieldConstraint& constraint) const noexcept {
    const auto value = std::hash<NodeId>{}(constraint.value);
    const auto field = std::hash<FieldId>{}(constraint.field);
    const auto target = std::hash<NodeId>{}(constraint.target);
    const auto combined = value ^ (field + 0x9e3779b9U + (value << 6U) + (value >> 2U));
    return combined ^ (target + 0x9e3779b9U + (combined << 6U) + (combined >> 2U));
}

ObjectId AndersenPointerAnalysis::unknownObject() const noexcept {
    return std::numeric_limits<ObjectId>::max();
}

void AndersenPointerAnalysis::addUnknown(NodeId value) {
    addAddressOf(value, unknownObject());
}

void AndersenPointerAnalysis::registerContentLoad(ObjectId base, NodeId target) {
    if (base == unknownObject()) {
        addValueFact(target, unknownObject());
        return;
    }
    if (!content_targets_[base].insert(target).second) return;
    all_content_targets_.insert(target);
    for (const auto candidate : {base, unknownObject()}) {
        const auto contents = object_contents_.find(candidate);
        if (contents != object_contents_.end())
            for (const auto object : contents->second) addValueFact(target, object);
        if (base == unknownObject()) break;
    }
}

void AndersenPointerAnalysis::registerFieldLoad(ObjectId base,
                                                const FieldConstraint& load) {
    if (base == unknownObject()) {
        addValueFact(load.target, unknownObject());
        return;
    }
    bool inserted = false;
    if (load.field == 0) {
        inserted = any_field_targets_[base].insert(load.target).second;
    } else {
        inserted = exact_field_targets_[{base, load.field}].insert(load.target).second;
    }
    if (!inserted) return;
    if (load.field == 0)
        all_any_field_targets_.insert(load.target);
    else
        field_targets_by_field_[load.field].insert(load.target);
    field_targets_by_base_[base].insert(load.target);
    all_field_targets_.insert(load.target);
    const auto propagate = [&](ObjectId object, FieldId field) {
        const auto facts = fields_.find({object, field});
        if (facts != fields_.end())
            for (const auto pointee : facts->second) addValueFact(load.target, pointee);
    };
    for (const auto candidate : {base, unknownObject()}) {
        if (load.field == 0) {
            const auto names = fields_by_base_.find(candidate);
            if (names != fields_by_base_.end())
                for (const auto field : names->second) propagate(candidate, field);
        } else {
            propagate(candidate, load.field);
            propagate(candidate, FieldId{0});
        }
        if (base == unknownObject()) break;
    }
}

void AndersenPointerAnalysis::solve() {
    ++solver_iteration_count_;
    for (;;) {
    while (indexed_copies_ < copies_.size()) {
        const auto constraint = copies_[indexed_copies_++];
        copy_targets_[constraint.first].push_back(constraint.second);
        const auto facts = points_to_.find(constraint.first);
        if (facts != points_to_.end())
            for (const auto object : facts->second) addValueFact(constraint.second, object);
    }
    while (indexed_loads_ < loads_.size()) {
        const auto constraint = loads_[indexed_loads_++];
        loads_by_pointer_[constraint.first].push_back(constraint);
        const auto facts = points_to_.find(constraint.first);
        if (facts != points_to_.end())
            for (const auto base : facts->second) registerContentLoad(base, constraint.second);
    }
    while (indexed_stores_ < stores_.size()) {
        const auto constraint = stores_[indexed_stores_++];
        stores_by_source_[constraint.first].push_back(constraint);
        stores_by_pointer_[constraint.second].push_back(constraint);
        const auto sources = points_to_.find(constraint.first);
        const auto bases = points_to_.find(constraint.second);
        if (sources != points_to_.end() && bases != points_to_.end())
            for (const auto base : bases->second)
                for (const auto object : sources->second)
                    addContentFact(base, object);
    }
    while (indexed_field_loads_ < field_loads_.size()) {
        const auto constraint = field_loads_[indexed_field_loads_++];
        field_loads_by_base_[constraint.value].push_back(constraint);
        const auto bases = points_to_.find(constraint.value);
        if (bases != points_to_.end())
            for (const auto base : bases->second) registerFieldLoad(base, constraint);
    }
    while (indexed_field_stores_ < field_stores_.size()) {
        const auto constraint = field_stores_[indexed_field_stores_++];
        field_stores_by_source_[constraint.value].push_back(constraint);
        field_stores_by_base_[constraint.target].push_back(constraint);
        const auto sources = points_to_.find(constraint.value);
        const auto bases = points_to_.find(constraint.target);
        if (sources != points_to_.end() && bases != points_to_.end())
            for (const auto base : bases->second)
                for (const auto object : sources->second)
                    addFieldFact({base, constraint.field}, object);
    }

    while (!value_worklist_.empty() || !content_worklist_.empty() ||
           !field_worklist_.empty()) {
        while (!value_worklist_.empty()) {
            const auto [value, object] = value_worklist_.front();
            value_worklist_.pop_front();

            if (const auto keyed = keyed_loads_by_key_.find(value);
                keyed != keyed_loads_by_key_.end()) {
                const auto found = fields_by_key_object_.find(object);
                if (found != fields_by_key_object_.end()) {
                    const auto field = found->second;
                    for (const auto index : keyed->second)
                        if (keyed_load_fields_[index].insert(field).second) {
                            const auto& load = keyed_field_loads_[index];
                            addFieldLoad(load.base, field, load.target);
                        }
                }
            }

            if (const auto copies = copy_targets_.find(value); copies != copy_targets_.end())
                for (const auto target : copies->second) addValueFact(target, object);
            if (const auto loads = loads_by_pointer_.find(value);
                loads != loads_by_pointer_.end())
                for (const auto& load : loads->second) registerContentLoad(object, load.second);
            if (const auto stores = stores_by_pointer_.find(value);
                stores != stores_by_pointer_.end())
                for (const auto& store : stores->second) {
                    const auto sources = points_to_.find(store.first);
                    if (sources == points_to_.end()) continue;
                    for (const auto pointee : sources->second)
                        addContentFact(object, pointee);
                }
            if (const auto stores = stores_by_source_.find(value);
                stores != stores_by_source_.end())
                for (const auto& store : stores->second) {
                    const auto bases = points_to_.find(store.second);
                    if (bases != points_to_.end())
                        for (const auto base : bases->second) addContentFact(base, object);
                }
            if (const auto loads = field_loads_by_base_.find(value);
                loads != field_loads_by_base_.end())
                for (const auto& load : loads->second) registerFieldLoad(object, load);
            if (const auto stores = field_stores_by_base_.find(value);
                stores != field_stores_by_base_.end())
                for (const auto& store : stores->second) {
                    const auto sources = points_to_.find(store.value);
                    if (sources == points_to_.end()) continue;
                    for (const auto pointee : sources->second)
                        addFieldFact({object, store.field}, pointee);
                }
            if (const auto stores = field_stores_by_source_.find(value);
                stores != field_stores_by_source_.end())
                for (const auto& store : stores->second) {
                    const auto bases = points_to_.find(store.target);
                    if (bases != points_to_.end())
                        for (const auto base : bases->second)
                            addFieldFact({base, store.field}, object);
                }
        }

        while (!content_worklist_.empty()) {
            const auto [base, object] = content_worklist_.front();
            content_worklist_.pop_front();
            const auto current = object_contents_.find(base);
            if (current == object_contents_.end() || !current->second.count(object)) continue;
            if (base == unknownObject()) {
                for (const auto target : all_content_targets_) addValueFact(target, object);
            } else if (const auto targets = content_targets_.find(base);
                       targets != content_targets_.end()) {
                for (const auto target : targets->second) addValueFact(target, object);
            }
        }

        while (!field_worklist_.empty()) {
            const auto fact = field_worklist_.front();
            field_worklist_.pop_front();
            const auto current = fields_.find(fact.location);
            if (current == fields_.end() || !current->second.count(fact.object)) continue;
            const auto propagate = [&](const TargetSet& targets) {
                for (const auto target : targets)
                    addValueFact(target, fact.object);
            };
            if (fact.location.base == unknownObject() && fact.location.field == 0) {
                propagate(all_field_targets_);
            } else if (fact.location.base == unknownObject()) {
                const auto exact =
                    field_targets_by_field_.find(fact.location.field);
                if (exact != field_targets_by_field_.end())
                    propagate(exact->second);
                propagate(all_any_field_targets_);
            } else if (fact.location.field == 0) {
                const auto targets =
                    field_targets_by_base_.find(fact.location.base);
                if (targets != field_targets_by_base_.end())
                    propagate(targets->second);
            } else {
                const auto exact = exact_field_targets_.find(fact.location);
                if (exact != exact_field_targets_.end())
                    propagate(exact->second);
                const auto any = any_field_targets_.find(fact.location.base);
                if (any != any_field_targets_.end())
                    propagate(any->second);
            }
        }
        }
        // Use the summary field only when no concrete key identity survived
        // the current fixed point. Delaying this choice prevents a temporary
        // unknown import/parameter fact from polluting a later exact index.
        for (std::size_t index = 0U; index < keyed_field_loads_.size(); ++index) {
            if (!keyed_load_fields_[index].empty()) continue;
            const auto& load = keyed_field_loads_[index];
            const auto keys = points_to_.find(load.key);
            if (keys == points_to_.end() || keys->second.empty()) continue;
            keyed_load_fields_[index].insert(FieldId{0U});
            addFieldLoad(load.base, FieldId{0U}, load.target);
        }
        if (indexed_copies_ == copies_.size() &&
            indexed_loads_ == loads_.size() &&
            indexed_stores_ == stores_.size() &&
            indexed_field_loads_ == field_loads_.size() &&
            indexed_field_stores_ == field_stores_.size())
            break;
    }
}

AndersenPointerAnalysis::Statistics
AndersenPointerAnalysis::statistics() const noexcept {
    std::size_t points_to_fact_count = 0U;
    for (const auto& [value, objects] : points_to_) {
        static_cast<void>(value);
        points_to_fact_count += objects.size();
    }
    std::size_t content_fact_count = 0U;
    for (const auto& [base, objects] : object_contents_) {
        static_cast<void>(base);
        content_fact_count += objects.size();
    }
    std::size_t field_fact_count = 0U;
    for (const auto& [location, objects] : fields_) {
        static_cast<void>(location);
        field_fact_count += objects.size();
    }
    return {
        used_values_.size(),
        used_objects_.size(),
        address_constraint_count_ + field_address_constraint_count_ +
            copies_.size() + loads_.size() + stores_.size() +
            field_loads_.size() + field_stores_.size() +
            keyed_field_loads_.size(),
        points_to_fact_count,
        content_fact_count,
        field_fact_count,
        solver_iteration_count_,
    };
}

const PointsToSet& AndersenPointerAnalysis::pointsTo(NodeId value) const {
    if (value == 0) throw std::invalid_argument("PTA value id zero is reserved");
    const auto it = points_to_.find(value);
    return it == points_to_.end() ? empty_ : it->second;
}

std::vector<AndersenPointerAnalysis::ContentFactView>
AndersenPointerAnalysis::contentFacts() const {
    std::vector<ContentFactView> result;
    for (const auto& [base, objects] : object_contents_)
        for (const auto object : objects) result.push_back({base, object});
    std::sort(result.begin(), result.end(), [](const auto& first, const auto& second) {
        return std::tie(first.base, first.object) < std::tie(second.base, second.object);
    });
    return result;
}

std::vector<AndersenPointerAnalysis::FieldFactView>
AndersenPointerAnalysis::fieldFacts() const {
    std::vector<FieldFactView> result;
    for (const auto& [location, objects] : fields_)
        for (const auto object : objects)
            result.push_back({location.base, location.field, object});
    std::sort(result.begin(), result.end(), [](const auto& first, const auto& second) {
        return std::tie(first.base, first.field, first.object) <
               std::tie(second.base, second.field, second.object);
    });
    return result;
}

}  // namespace cpygraph
