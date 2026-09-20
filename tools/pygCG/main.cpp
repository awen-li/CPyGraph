#include "api/package.h"
#include "common/package_cli.h"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

constexpr char kToolName[] = "pygCG";
constexpr char kComponentName[] = "cg";
constexpr std::string_view kModuleCodeName = "<module>";
constexpr std::string_view kLambdaCodeName = "<lambda>";
constexpr std::string_view kListComprehensionCodeName = "<listcomp>";
constexpr std::string_view kDictComprehensionCodeName = "<dictcomp>";
constexpr std::string_view kSetComprehensionCodeName = "<setcomp>";
constexpr std::string_view kGeneratorExpressionCodeName = "<genexpr>";
constexpr std::string_view kBuiltinImportName = "<builtin>.__import__";
constexpr std::string_view kImportModuleName = "importlib.import_module";
constexpr std::string_view kBuiltinInputName = "<builtin>.input";
constexpr std::string_view kBuiltinEvalName = "<builtin>.eval";
constexpr std::string_view kBuiltinExecName = "<builtin>.exec";
constexpr std::string_view kBuiltinLenName = "<builtin>.len";
constexpr std::string_view kBuiltinMapName = "<builtin>.map";
constexpr std::string_view kBuiltinRangeName = "<builtin>.range";
constexpr std::string_view kBuiltinSuperName = "<builtin>.super";
constexpr std::string_view kBuiltinDictItemsName = "<**PyDict**>.items";
constexpr std::string_view kBuiltinStrJoinName = "<**PyStr**>.join";
constexpr std::string_view kBuiltinStrSplitName = "<**PyStr**>.split";

std::string_view externalCalleeName(
    cpygraph::bytecode::PythonExternalCallee callee) noexcept {
    using E = cpygraph::bytecode::PythonExternalCallee;
    switch (callee) {
        case E::Import: return kBuiltinImportName;
        case E::ImportModule: return kImportModuleName;
        case E::Input: return kBuiltinInputName;
        case E::Eval: return kBuiltinEvalName;
        case E::Exec: return kBuiltinExecName;
        case E::Len: return kBuiltinLenName;
        case E::Map: return kBuiltinMapName;
        case E::Range: return kBuiltinRangeName;
        case E::Super: return kBuiltinSuperName;
        case E::DictItems: return kBuiltinDictItemsName;
        case E::StrJoin: return kBuiltinStrJoinName;
        case E::StrSplit: return kBuiltinStrSplitName;
        case E::None: return {};
    }
    return {};
}

bool isImplicitFrame(std::string_view name) noexcept {
    return name == kListComprehensionCodeName ||
           name == kDictComprehensionCodeName ||
           name == kSetComprehensionCodeName ||
           name == kGeneratorExpressionCodeName;
}

// Presentation names reconstruct lexical ownership for CPython releases whose
// code objects do not expose co_qualname. Core graph nodes remain numeric.
class QualifiedNames {
public:
    explicit QualifiedNames(const cpygraph::package::PackageAnalysis& package)
        : package_(package) {
        std::unordered_map<cpygraph::CodeObjectId, std::size_t> lambda_counts;
        for (const auto& code : package.codeObjects()) {
            if (code.name == kModuleCodeName) {
                names_.emplace(code.id, code.module_name);
                continue;
            }
            auto component = code.name;
            if (code.name == kLambdaCodeName)
                component = "<lambda" +
                    std::to_string(++lambda_counts[code.parent]) + ">";
            names_.emplace(code.id, names_.at(code.parent) + "." + component);
        }
    }

    const std::string& name(cpygraph::CodeObjectId id) const {
        return names_.at(id);
    }

    cpygraph::CodeObjectId projectedCaller(cpygraph::CodeObjectId id) const {
        while (isImplicitFrame(package_.codeObject(id).name))
            id = package_.codeObject(id).parent;
        return id;
    }

    bool isImplicit(cpygraph::CodeObjectId id) const {
        return isImplicitFrame(package_.codeObject(id).name);
    }

private:
    const cpygraph::package::PackageAnalysis& package_;
    std::unordered_map<cpygraph::CodeObjectId, std::string> names_;
};

void analyzeCallGraph(const cpygraph::package::PackageAnalysis& package,
                      std::ostream& output) {
    const auto result = cpygraph::package::GraphBuilder().callGraph(package);
    const QualifiedNames names(package);
    std::size_t resolved = 0;
    std::size_t unresolved = 0;
    std::set<std::pair<std::string, std::string>> edges;
    for (const auto& edge : result.graph.edges()) {
        if (edge.target) {
            ++resolved;
            // PyCG's source-level relation does not expose the implicit frame
            // used to execute a comprehension. Project calls from that frame
            // onto its lexical owner and omit the frame-activation edge.
            if (!names.isImplicit(edge.target->code))
                edges.emplace(names.name(names.projectedCaller(edge.caller)),
                              names.name(edge.target->code));
        } else {
            ++unresolved;
            const auto external = externalCalleeName(
                result.graph.unresolvedGroup(edge.unresolved_group)
                    .external_callee);
            if (!external.empty())
                edges.emplace(
                    names.name(names.projectedCaller(edge.caller)), external);
        }
    }

    output << "\"component\":\"" << kComponentName
           << "\",\"modules\":" << package.moduleCount()
           << ",\"discovered_code_objects\":" << package.codeObjects().size()
           << ",\"analyzed_code_objects\":"
           << result.analyzed_code_object_count
           << ",\"activated_callables\":"
           << result.activated_callable_count
           << ",\"callsites\":" << result.call_site_count
           << ",\"resolved_edges\":" << resolved
           << ",\"unresolved_edges\":" << unresolved
           << ",\"edges\":[";
    bool first = true;
    for (const auto& [caller, callee] : edges) {
        if (!first) output << ',';
        first = false;
        output << '[' << std::quoted(caller) << ',' << std::quoted(callee)
               << ']';
    }
    output << ']';
}

}  // namespace

int main(int argc, char** argv) {
    return cpygraph::tools::runPackageTool(argc, argv, kToolName,
                                           analyzeCallGraph);
}
