#pragma once

#include "common/model.h"

#include <unordered_set>
#include <cstdint>
#include <string_view>

namespace cpygraph {

using PointsToSet = std::unordered_set<ObjectId>;

class PointerAnalysis {
public:
    virtual ~PointerAnalysis() = default;
    virtual NodeId newValue() = 0;
    virtual ObjectId newObject() = 0;
    virtual void addAddressOf(NodeId value, ObjectId object) = 0;
    virtual void addCopy(NodeId source, NodeId target) = 0;
    virtual void addLoad(NodeId pointer, NodeId target) = 0;
    virtual void addStore(NodeId source, NodeId pointer) = 0;
    virtual FieldId internField(std::string_view field) = 0;
    virtual FieldId internIndexField(std::int64_t index) = 0;
    virtual void bindFieldKey(ObjectId object, FieldId field) = 0;
    virtual std::string_view debugField(FieldId field) const noexcept = 0;
    virtual void addFieldAddress(ObjectId base, FieldId field, ObjectId object) = 0;
    virtual void addFieldLoad(NodeId base, FieldId field, NodeId target) = 0;
    virtual void addKeyedFieldLoad(NodeId base, NodeId key,
                                   NodeId target) = 0;
    virtual void addFieldStore(NodeId source, NodeId base, FieldId field) = 0;
    virtual void addUnknown(NodeId value) = 0;
    virtual ObjectId unknownObject() const noexcept = 0;
    virtual void solve() = 0;
    virtual const PointsToSet& pointsTo(NodeId value) const = 0;
};

}  // namespace cpygraph
