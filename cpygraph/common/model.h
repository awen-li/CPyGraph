#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cpygraph {

using NodeId = std::uint64_t;
using ObjectId = std::uint64_t;
using CodeObjectId = std::uint64_t;
using FunctionId = std::uint64_t;
using SymbolId = std::uint32_t;
using FieldId = std::uint32_t;

class DebugStringTable {
public:
    SymbolId intern(std::string value) {
        const auto found = ids_.find(value);
        if (found != ids_.end()) return found->second;
        const auto id = static_cast<SymbolId>(values_.size() + 1);
        values_.push_back(std::move(value));
        ids_.emplace(values_.back(), id);
        return id;
    }

    std::string_view lookup(SymbolId id) const {
        if (id == 0 || id > values_.size()) return {};
        return values_[id - 1];
    }

    SymbolId find(std::string_view value) const {
        const auto found = ids_.find(std::string(value));
        return found == ids_.end() ? 0U : found->second;
    }

private:
    std::vector<std::string> values_;
    std::unordered_map<std::string, SymbolId> ids_;
};

}  // namespace cpygraph
