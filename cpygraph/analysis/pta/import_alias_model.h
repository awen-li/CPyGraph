#pragma once

#include "analysis/pta/pointer_analysis.h"

#include <string>

namespace cpygraph {

// Generates field-sensitive PTA constraints for Python import bindings.
class ImportAliasModel {
public:
    explicit ImportAliasModel(PointerAnalysis& analysis) noexcept : analysis_(analysis) {}

    void bindModule(NodeId binding, ObjectId module);
    void bindAlias(NodeId source, NodeId alias);
    void defineAttribute(ObjectId module, std::string name, ObjectId value);
    void loadAttribute(NodeId base, std::string name, NodeId result);
    void bindFromImport(NodeId module_binding, std::string name, NodeId local_binding);

private:
    PointerAnalysis& analysis_;
};

}  // namespace cpygraph
