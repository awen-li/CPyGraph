#include "package/analysis.h"

#include "analysis/cfg/cfg_builder.h"
#include "bytecode/adapters/adapter_factory.h"
#include "analysis/profile/profiler.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cpygraph::package {
namespace {

constexpr CodeObjectId kFirstCodeObjectId = 1;

struct PendingCodeObject {
    CodeObjectId id{};
    CodeObjectId parent{};
    const LoadedModule* module{};
    const bytecode::LoadedCodeObject* code{};
};

void discoverCodeObjects(const LoadedModule& module,
                         const bytecode::LoadedCodeObject& code,
                         CodeObjectId parent, CodeObjectId& next_id,
                         std::vector<PendingCodeObject>& pending) {
    const auto id = next_id++;
    pending.push_back({id, parent, &module, &code});
    for (const auto& child : code.nested)
        discoverCodeObjects(module, child, id, next_id, pending);
}

std::vector<std::string> localNames(const bytecode::LoadedCodeObject& code) {
    return code.metadata.locals;
}

}  // namespace

PackageAnalysis::PackageAnalysis(std::filesystem::path source_root,
                                 std::string python_version,
                                 std::size_t module_count,
                                 std::vector<CodeObjectAnalysis> code_objects)
    : source_root_(std::move(source_root)), python_version_(std::move(python_version)),
      module_count_(module_count), code_objects_(std::move(code_objects)) {}

const CodeObjectAnalysis& PackageAnalysis::codeObject(CodeObjectId id) const {
    const auto found = std::find_if(
        code_objects_.begin(), code_objects_.end(),
        [id](const auto& code) { return code.id == id; });
    if (found == code_objects_.end())
        throw std::out_of_range("unknown package code-object id " + std::to_string(id));
    return *found;
}

PackageAnalysis PackageAnalyzer::analyze(const LoadedPackage& package) const {
    profile::Scope profile_scope(profile::Phase::BytecodeAnalysis);
    auto adapter = bytecode::AdapterFactory::create(package.pythonVersion());
    std::vector<PendingCodeObject> pending;
    CodeObjectId next_id = kFirstCodeObjectId;
    for (const auto& module : package.modules())
        discoverCodeObjects(module, module.code, 0, next_id, pending);

    std::unordered_map<const bytecode::LoadedCodeObject*, CodeObjectId> ids;
    for (const auto& item : pending) ids.emplace(item.code, item.id);

    std::vector<CodeObjectAnalysis> result;
    result.reserve(pending.size());
    for (const auto& item : pending) {
        std::vector<CodeObjectId> constant_code_objects(item.code->constants.size(), 0);
        for (std::size_t index = 0; index < item.code->constants.size(); ++index) {
            const auto& constant = item.code->constants[index];
            if (constant.kind != bytecode::LoadedConstant::Kind::CodeObject) continue;
            if (constant.nested_code_index >= item.code->nested.size())
                throw std::runtime_error("code-object constant has an invalid nested index");
            constant_code_objects[index] = ids.at(
                &item.code->nested[constant.nested_code_index]);
        }
        auto program = adapter->lift(item.code->bytecode, item.code->metadata);
        const auto exception_regions = [&] {
#ifdef CPYGRAPH_ABLATE_EXCEPTION_FLOW
            return std::vector<bytecode::ExceptionRegion>{};
#else
            return adapter->exceptionRegions(
                item.code->bytecode, item.code->metadata);
#endif
        }();
        auto graph = cfg::CFGBuilder().build(std::move(program), exception_regions);
        result.push_back({
            item.id,
            item.parent,
            item.module->name,
            item.code->name,
            item.code->qualname,
            item.code->filename,
            item.module->is_package,
            item.code->argument_count,
            item.code->positional_only_argument_count,
            item.code->keyword_only_argument_count,
            item.code->has_var_arguments,
            item.code->has_var_keywords,
            localNames(*item.code),
            std::move(constant_code_objects),
            std::move(graph),
            item.code->metadata.free_names,
        });
    }
    return PackageAnalysis(package.sourceRoot(), std::string(package.pythonVersion()),
                           package.modules().size(), std::move(result));
}

}  // namespace cpygraph::package
