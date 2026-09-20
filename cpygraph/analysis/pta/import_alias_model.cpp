#include "analysis/pta/import_alias_model.h"

namespace cpygraph {

void ImportAliasModel::bindModule(NodeId binding, ObjectId module) {
    analysis_.addAddressOf(binding, module);
}

void ImportAliasModel::bindAlias(NodeId source, NodeId alias) {
    analysis_.addCopy(source, alias);
}

void ImportAliasModel::defineAttribute(ObjectId module, std::string name, ObjectId value) {
    analysis_.addFieldAddress(module, analysis_.internField(name), value);
}

void ImportAliasModel::loadAttribute(NodeId base, std::string name, NodeId result) {
    analysis_.addFieldLoad(base, analysis_.internField(name), result);
}

void ImportAliasModel::bindFromImport(NodeId module_binding, std::string name,
                                      NodeId local_binding) {
    loadAttribute(module_binding, std::move(name), local_binding);
}

}  // namespace cpygraph
