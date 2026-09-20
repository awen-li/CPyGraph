#include "api/package.h"
#include "test_support.h"

#include <algorithm>
#include <cstddef>

#ifndef CPYGRAPH_TEST_PYTHON_FEATURE_PACKAGE
#error "CPYGRAPH_TEST_PYTHON_FEATURE_PACKAGE must identify the feature fixture"
#endif

namespace {

constexpr char kInitializerName[] = "__init__";
constexpr char kEnterName[] = "__enter__";
constexpr char kExitName[] = "__exit__";
constexpr char kAddName[] = "__add__";
constexpr char kExerciseName[] = "exercise";
constexpr char kImportedName[] = "imported";
constexpr std::size_t kMinimumImplicitEdges = 1;

}  // namespace

int main() {
    const auto compiled = cpygraph::package::PackageCompiler().compile(
        CPYGRAPH_TEST_PYTHON_FEATURE_PACKAGE);
    const auto loaded = cpygraph::package::PackageLoader().load(compiled);
    const auto package = cpygraph::package::PackageAnalyzer().analyze(loaded);

    cpygraph::CodeObjectId initializer = 0;
    cpygraph::CodeObjectId enter = 0;
    cpygraph::CodeObjectId exit = 0;
    cpygraph::CodeObjectId add = 0;
    cpygraph::CodeObjectId exercise = 0;
    cpygraph::CodeObjectId imported = 0;
    for (const auto& code : package.codeObjects()) {
        if (code.name == kInitializerName) initializer = code.id;
        if (code.name == kEnterName) enter = code.id;
        if (code.name == kExitName) exit = code.id;
        if (code.name == kAddName) add = code.id;
        if (code.name == kExerciseName) exercise = code.id;
        if (code.name == kImportedName) imported = code.id;
    }
    require(initializer != 0U && enter != 0U && exit != 0U && add != 0U,
            "native class bytecode preserves context protocol code objects");
    require(exercise != 0U && imported != 0U,
            "feature fixture preserves caller and imported function identities");

    cpygraph::package::PackageAnalysisObservations observations;
    const auto result = cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
        package, {}, &observations);
    bool resolved_import = false;
    bool resolved_initializer = false;
    bool resolved_enter = false;
    bool resolved_exit = false;
    bool resolved_add = false;
    bool grouped_exit = false;
    bool grouped_add = false;
    std::size_t implicit_or_dynamic_edges = 0;
    for (const auto& edge : result.graph.edges()) {
        if (edge.caller != exercise) continue;
        if (edge.target && edge.target->code == imported) resolved_import = true;
        if (edge.target && edge.target->code == initializer)
            resolved_initializer = true;
        if (edge.target && edge.target->code == enter) resolved_enter = true;
        if (edge.target && edge.target->code == exit) resolved_exit = true;
        if (edge.target && edge.target->code == add) resolved_add = true;
        if (!edge.target) {
            ++implicit_or_dynamic_edges;
            if (edge.unresolved_group != 0U) {
                const auto& group = result.graph.unresolvedGroup(
                    edge.unresolved_group);
                grouped_exit |= cpygraph::bytecode::containsPythonMethod(
                    group.candidate_methods,
                    cpygraph::bytecode::PythonSpecialMethod::Exit);
                grouped_add |= cpygraph::bytecode::containsPythonMethod(
                    group.candidate_methods,
                    cpygraph::bytecode::PythonSpecialMethod::Add);
            }
        }
    }
    require(resolved_import,
            "relative import resolves across real compiled package modules");
    require(resolved_initializer && resolved_enter && resolved_exit &&
                resolved_add,
            "user-defined constructor and protocol hooks have concrete call edges");
    require(grouped_exit,
            "dynamic context cleanup alternatives retain the __exit__ group label");
    require(grouped_add,
            "dynamic operator alternatives retain the __add__ group label");
    require(implicit_or_dynamic_edges >= kMinimumImplicitEdges,
            "constructor, descriptor, and context cleanup dispatch is never silently dropped");
    require(result.analyzed_code_object_count == package.codeObjects().size(),
            "reachable user-defined protocol method bodies are analyzed");

    const auto field_name = [&](cpygraph::FieldId field) {
        for (const auto& entry : observations.attribute_field_names)
            if (entry.field == field) return entry.name;
        return std::string{};
    };
    cpygraph::ObjectId sys_module = 0U;
    for (const auto& module : observations.module_objects)
        if (module.name == "sys") sys_module = module.object;
    require(sys_module != 0U,
            "module-object observations retain exact imported identities");
    bool observed_store = false;
    bool observed_load = false;
    bool observed_sys_argv = false;
    std::vector<cpygraph::NodeId> sys_argv_values;
    for (const auto& access : observations.attribute_accesses) {
        require(!access.base_values.empty() && !access.values.empty(),
                "attribute observations retain receiver and value roles");
        if (field_name(access.field) == "argv" &&
            access.kind ==
                cpygraph::package::PTAAttributeAccessKind::Load) {
            bool exact_sys_access = false;
            for (const auto base : access.base_values)
                for (const auto& points_to : observations.points_to)
                    if (points_to.value == base &&
                        std::find(points_to.objects.begin(),
                                  points_to.objects.end(), sys_module) !=
                            points_to.objects.end()) {
                        observed_sys_argv = true;
                        exact_sys_access = true;
                    }
            if (exact_sys_access)
                sys_argv_values.insert(
                    sys_argv_values.end(), access.values.begin(),
                    access.values.end());
        }
        if (field_name(access.field) != "value") continue;
        observed_store |= access.code == initializer &&
            access.kind == cpygraph::package::PTAAttributeAccessKind::Store;
        observed_load |= access.code == add &&
            access.kind == cpygraph::package::PTAAttributeAccessKind::Load;
    }
    require(observed_store && observed_load,
            "attribute observations distinguish instruction-specific loads and stores");
    require(observed_sys_argv,
            "attribute observations can be qualified by exact module receiver");
    require(std::any_of(
                observations.element_loads.begin(),
                observations.element_loads.end(), [&](const auto& load) {
                    return !load.key_values.empty() &&
                        !load.values.empty() &&
                        std::any_of(
                            load.base_values.begin(), load.base_values.end(),
                            [&](const auto base) {
                                return std::find(
                                    sys_argv_values.begin(),
                                    sys_argv_values.end(), base) !=
                                    sys_argv_values.end();
                            });
                }),
            "item-load observations preserve exact base, key, and result roles");
}
