#include "api/package.h"
#include "test_support.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <unistd.h>

#ifndef CPYGRAPH_TEST_PACKAGE_PATH
#error "CPYGRAPH_TEST_PACKAGE_PATH must identify the package fixture"
#endif

namespace {

constexpr std::size_t kExpectedModuleCount = 3;
constexpr std::size_t kExpectedSingleModuleCount = 1;
constexpr std::size_t kExpectedCodeObjectCount = 7;
constexpr std::size_t kExpectedPTAAnalyzedCodeObjectCount = 6;
constexpr std::size_t kExpectedImportedCallEdges = 2;
constexpr std::size_t kMinimumObservationCount = 1;
constexpr std::size_t kExpectedAliasedModuleCount = 2;
constexpr std::size_t kExpectedCompleteConstructorEdges = 2;
constexpr std::size_t kExpectedCallbackContexts = 2;
constexpr std::size_t kExpectedClosureCallSites = 2;
constexpr std::size_t kMinimumMetaclassDispatchEdges = 1U;
constexpr std::size_t kMinimumMetaclassConstructorEdges = 1U;
constexpr std::size_t kExpectedMixedLanguageModuleCount = 1;
constexpr char kPackageModuleName[] = "sample_package";
constexpr char kWorkflowModuleName[] = "sample_package.workflow";
constexpr char kNestedFunctionName[] = "identity";
constexpr char kImportedFunctionName[] = "imported";
constexpr char kRunFunctionName[] = "run";
constexpr char kAliasModuleName[] = "alias";
constexpr char kImplementationModuleName[] = "implementation";
constexpr char kMixedLanguagePythonModuleName[] = "python.application";
constexpr char kDirectBytecodeModuleName[] = "standalone";
constexpr char kMixedLanguagePythonSource[] = "value = 1\n";
constexpr char kMixedLanguageNativeSource[] = "pub fn value() -> i32 { 1 }\n";
constexpr char kRepeatedMetaclassSource[] =
    "class Meta(type):\n"
    "    def __call__(cls):\n"
    "        return type.__call__(cls)\n"
    "def build():\n"
    "    class Product(metaclass=Meta):\n"
    "        def __init__(self):\n"
    "            self.value = 1\n"
    "    return Product()\n"
    "def left():\n"
    "    return build()\n"
    "def right():\n"
    "    return build()\n"
    "left()\n"
    "right()\n";
constexpr std::streamoff kPycMagicByteOffset = 0;
constexpr char kInvalidPycMagicByte = 0;

class TemporaryPackageTree {
public:
    TemporaryPackageTree()
        : root_(std::filesystem::temp_directory_path() /
                ("cpygraph-package-containment-" +
                 std::to_string(static_cast<long long>(::getpid())))) {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::create_directories(root_);
    }

    ~TemporaryPackageTree() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    const std::filesystem::path& root() const noexcept { return root_; }

private:
    std::filesystem::path root_;
};

}  // namespace

int main() {
    {
        TemporaryPackageTree tree;
        const auto source = tree.root() / "standalone.py";
        const auto bytecode = tree.root() / "standalone.pyc";
        std::ofstream(source) << "value = 1\n";
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(source);
        std::filesystem::copy_file(compiled.modules().front().bytecode_path,
                                   bytecode);
        std::filesystem::remove(source);

        const auto loaded =
            cpygraph::package::PackageLoader().loadBytecode(bytecode);
        require(loaded.modules().size() == kExpectedSingleModuleCount,
                "direct bytecode loading emits one module");
        require(loaded.modules().front().name == kDirectBytecodeModuleName &&
                    loaded.modules().front().code.module_name ==
                        kDirectBytecodeModuleName,
                "direct bytecode loading derives a stable module identity");
        const auto analysis =
            cpygraph::package::PackageAnalyzer().analyze(loaded);
        require(analysis.moduleCount() == kExpectedSingleModuleCount,
                "direct bytecode input reaches package analysis without source");
    }

    {
        TemporaryPackageTree tree;
        const auto source = tree.root() / "versioned.py";
        std::ofstream(source) << "value = 1\n";
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(source);
        require(compiled.modules().size() == kExpectedSingleModuleCount,
                "single-source compilation emits one bytecode module");
        {
            std::fstream bytecode(compiled.modules().front().bytecode_path,
                                  std::ios::binary | std::ios::in |
                                      std::ios::out);
            require(static_cast<bool>(bytecode),
                    "compiled bytecode remains available for validation");
            bytecode.seekp(kPycMagicByteOffset);
            bytecode.put(kInvalidPycMagicByte);
        }
        bool rejected_wrong_magic = false;
        try {
            cpygraph::package::PackageLoader().load(compiled);
        } catch (const std::runtime_error&) {
            rejected_wrong_magic = true;
        }
        require(rejected_wrong_magic,
                "package loader rejects bytecode for another CPython build");
    }

    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "package";
        const auto outside_source = tree.root() / "outside.py";
        std::filesystem::create_directories(package_root);
        std::ofstream(outside_source) << "value = 1\n";
        std::filesystem::create_symlink(outside_source,
                                        package_root / "escaped.py");
        bool rejected_escaped_source = false;
        try {
            cpygraph::package::PackageCompiler().compile(package_root);
        } catch (const std::invalid_argument&) {
            rejected_escaped_source = true;
        }
        require(rejected_escaped_source,
                "package compiler rejects sources resolving outside its root");
    }

    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "package";
        const auto implementation = package_root / "implementation.py";
        std::filesystem::create_directories(package_root);
        std::ofstream(implementation) << "value = 1\n";
        std::filesystem::create_symlink(implementation,
                                        package_root / "alias.py");
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(package_root);
        require(compiled.modules().size() == kExpectedAliasedModuleCount,
                "package compiler preserves every in-root module alias");
        const auto has_module = [&](const std::string& name) {
            return std::any_of(compiled.modules().begin(), compiled.modules().end(),
                               [&](const auto& module) {
                                   return module.name == name;
                               });
        };
        require(has_module(kAliasModuleName) &&
                    has_module(kImplementationModuleName),
                "symlinked sources retain their logical module identities");
    }

    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "mixed-language-package";
        const auto native_root = package_root / "src";
        const auto python_root = package_root / "python";
        std::filesystem::create_directories(native_root);
        std::filesystem::create_directories(python_root);
        std::ofstream(native_root / "lib.rs") << kMixedLanguageNativeSource;
        std::ofstream(python_root / "application.py") <<
            kMixedLanguagePythonSource;
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(package_root);
        require(compiled.modules().size() == kExpectedMixedLanguageModuleCount &&
                    compiled.modules().front().name ==
                        kMixedLanguagePythonModuleName,
                "non-Python src directories do not hide Python package sources");
    }

    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "repeated-metaclass-package";
        std::filesystem::create_directories(package_root);
        std::ofstream(package_root / "scenario.py") << kRepeatedMetaclassSource;
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(package_root);
        const auto loaded = cpygraph::package::PackageLoader().load(compiled);
        const auto analysis =
            cpygraph::package::PackageAnalyzer().analyze(loaded);
        cpygraph::CodeObjectId metaclass_call = 0U;
        cpygraph::CodeObjectId product_init = 0U;
        for (const auto& code : analysis.codeObjects()) {
            if (code.name == "__call__") metaclass_call = code.id;
            if (code.name == "__init__") product_init = code.id;
        }
        require(metaclass_call != 0U && product_init != 0U,
                "custom-metaclass fixture exposes dispatch and constructor code");
        cpygraph::PTASensitivityConfiguration complete;
        complete.level = cpygraph::PTASensitivityLevel::Complete;
        const auto graph =
            cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
                analysis, {}, nullptr, complete);
        const auto target_count = [&](cpygraph::CodeObjectId target) {
            return static_cast<std::size_t>(std::count_if(
                graph.graph.edges().begin(), graph.graph.edges().end(),
                [&](const auto& edge) {
                    return edge.target && edge.target->code == target;
                }));
        };
        require(target_count(metaclass_call) >= kMinimumMetaclassDispatchEdges &&
                    target_count(product_init) >=
                        kMinimumMetaclassConstructorEdges,
                "constructor inheritance does not overwrite custom metaclass dispatch");
    }

    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "sensitive-class-package";
        std::filesystem::create_directories(package_root);
        std::ofstream(package_root / "scenario.py")
            << "def run(flag):\n"
               "    class Local:\n"
               "        def __init__(self):\n"
               "            self.value = 1\n"
               "    if flag:\n"
               "        marker = 1\n"
               "    else:\n"
               "        marker = 2\n"
               "    return Local()\n"
               "run(True)\n";
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(package_root);
        const auto loaded_sensitive =
            cpygraph::package::PackageLoader().load(compiled);
        const auto sensitive_analysis =
            cpygraph::package::PackageAnalyzer().analyze(loaded_sensitive);
        cpygraph::CodeObjectId initializer = 0U;
        cpygraph::CodeObjectId class_body = 0U;
        cpygraph::CodeObjectId run = 0U;
        for (const auto& code : sensitive_analysis.codeObjects()) {
            if (code.name == "__init__") initializer = code.id;
            if (code.name == "Local") class_body = code.id;
            if (code.name == "run") run = code.id;
        }
        require(initializer != 0U && class_body != 0U && run != 0U,
                "complete-sensitivity class fixture exposes its code objects");

        cpygraph::PTASensitivityConfiguration complete;
        complete.level = cpygraph::PTASensitivityLevel::Complete;
        cpygraph::package::PackageAnalysisObservations observations;
        const auto graph =
            cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
                sensitive_analysis, {}, &observations, complete);
        const auto resolved_initializers = static_cast<std::size_t>(
            std::count_if(
                graph.graph.edges().begin(), graph.graph.edges().end(),
                [&](const auto& edge) {
                    return edge.target && edge.target->code == initializer;
                }));
        require(resolved_initializers >= kExpectedCompleteConstructorEdges,
                "complete PTA retains constructors cloned across path contexts");
        const auto eager_class_body = std::find_if(
            observations.eager_code_executions.begin(),
            observations.eager_code_executions.end(),
            [run, class_body](const auto& execution) {
                return execution.source == run &&
                       execution.target == class_body;
            });
        require(eager_class_body != observations.eager_code_executions.end(),
                "package observations expose eager class-body execution");
        const auto& defining_program =
            sensitive_analysis.codeObject(run).cfg.program();
        require(eager_class_body->instruction_index < defining_program.size() &&
                    defining_program[eager_class_body->instruction_index].opcode ==
                        cpygraph::bytecode::SemanticOpcode::CreateFunction,
                "eager execution retains its semantic definition site");
    }


    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "context-callback-package";
        std::filesystem::create_directories(package_root);
        std::ofstream(package_root / "scenario.py")
            << "def left():\n"
               "    return 1\n"
               "def right():\n"
               "    return 2\n"
               "def invoke(callback):\n"
               "    return callback()\n"
               "def left_entry():\n"
               "    return invoke(left)\n"
               "def right_entry():\n"
               "    return invoke(right)\n"
               "def choose():\n"
               "    return left\n"
               "def factory_entry():\n"
               "    return choose()()\n";
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(package_root);
        const auto loaded = cpygraph::package::PackageLoader().load(compiled);
        const auto callback_analysis =
            cpygraph::package::PackageAnalyzer().analyze(loaded);
        cpygraph::CodeObjectId left = 0U;
        cpygraph::CodeObjectId right = 0U;
        cpygraph::CodeObjectId invoke = 0U;
        cpygraph::CodeObjectId left_entry = 0U;
        cpygraph::CodeObjectId right_entry = 0U;
        cpygraph::CodeObjectId choose = 0U;
        cpygraph::CodeObjectId factory_entry = 0U;
        for (const auto& code : callback_analysis.codeObjects()) {
            if (code.name == "left") left = code.id;
            if (code.name == "right") right = code.id;
            if (code.name == "invoke") invoke = code.id;
            if (code.name == "left_entry") left_entry = code.id;
            if (code.name == "right_entry") right_entry = code.id;
            if (code.name == "choose") choose = code.id;
            if (code.name == "factory_entry") factory_entry = code.id;
        }
        require(left != 0U && right != 0U && invoke != 0U &&
                    left_entry != 0U && right_entry != 0U && choose != 0U &&
                    factory_entry != 0U,
                "context callback fixture exposes every code object");

        cpygraph::PTASensitivityConfiguration complete;
        complete.level = cpygraph::PTASensitivityLevel::Complete;
        cpygraph::package::PackageAnalysisObservations observations;
        const auto graph =
            cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
                callback_analysis, {left_entry, right_entry, factory_entry},
                &observations, complete);
        std::unordered_set<cpygraph::PTAContextId> invoke_contexts;
        for (const auto& activation : observations.call_activations)
            if (activation.target == invoke)
                invoke_contexts.insert(activation.context);
        require(invoke_contexts.size() == kExpectedCallbackContexts,
                "numeric call activations expose distinct callback contexts");
        for (const auto& site : observations.call_sites)
            if (site.code == invoke)
                require(invoke_contexts.count(site.context) != 0U,
                        "call activation context identifies the callee body instance");

        std::unordered_set<cpygraph::CodeObjectId> callback_targets;
        bool unresolved_callback = false;
        for (const auto& edge : graph.graph.edges()) {
            if (edge.caller != invoke) continue;
            if (edge.target)
                callback_targets.insert(edge.target->code);
            else
                unresolved_callback = true;
        }
        require(callback_targets ==
                    std::unordered_set<cpygraph::CodeObjectId>{left, right} &&
                    !unresolved_callback,
                "internally bound parameters do not retain artificial external values");
        bool resolved_factory_result = false;
        bool unresolved_factory_result = false;
        for (const auto& edge : graph.graph.edges()) {
            if (edge.caller != factory_entry) continue;
            if (edge.target && edge.target->code == left)
                resolved_factory_result = true;
            if (!edge.target)
                unresolved_factory_result = true;
        }
        require(resolved_factory_result && !unresolved_factory_result,
                "resolved in-package call returns do not retain fallback objects");
    }

    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "closure-capture-package";
        std::filesystem::create_directories(package_root);
        std::ofstream(package_root / "scenario.py")
            << "def left():\n"
               "    return 1\n"
               "def right():\n"
               "    return 2\n"
               "def bind(callback):\n"
               "    def middle():\n"
               "        def closure():\n"
               "            return callback()\n"
               "        return closure\n"
               "    return middle()\n"
               "def run():\n"
               "    left_closure = bind(left)\n"
               "    right_closure = bind(right)\n"
               "    return left_closure(), right_closure()\n"
               "run()\n";
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(package_root);
        const auto loaded = cpygraph::package::PackageLoader().load(compiled);
        const auto closure_analysis =
            cpygraph::package::PackageAnalyzer().analyze(loaded);
        cpygraph::CodeObjectId left = 0U;
        cpygraph::CodeObjectId right = 0U;
        cpygraph::CodeObjectId closure = 0U;
        for (const auto& code : closure_analysis.codeObjects()) {
            if (code.name == "left") left = code.id;
            if (code.name == "right") right = code.id;
            if (code.name == "closure") {
                closure = code.id;
                require(code.free_names == std::vector<std::string>{"callback"},
                        "package analysis retains ordered free-variable metadata");
            }
        }
        require(left != 0U && right != 0U && closure != 0U,
                "closure fixture exposes its callable code objects");

        cpygraph::PTASensitivityConfiguration complete;
        complete.level = cpygraph::PTASensitivityLevel::Complete;
        const auto graph = cpygraph::package::GraphBuilder().callGraph(
            closure_analysis, {}, complete);
        std::unordered_map<
            std::uint64_t, std::unordered_set<cpygraph::CodeObjectId>>
            targets_by_site;
        bool unresolved_closure_call = false;
        for (const auto& edge : graph.graph.edges()) {
            if (edge.caller != closure) continue;
            if (edge.target)
                targets_by_site[edge.site].insert(edge.target->code);
            else
                unresolved_closure_call = true;
        }
        std::unordered_set<cpygraph::CodeObjectId> all_targets;
        for (const auto& [site, targets] : targets_by_site) {
            (void)site;
            require(targets.size() == 1U,
                    "each context-specific closure activation has one callback target");
            all_targets.insert(targets.begin(), targets.end());
        }
        require(targets_by_site.size() == kExpectedClosureCallSites &&
                    all_targets ==
                        std::unordered_set<cpygraph::CodeObjectId>{left, right} &&
                    !unresolved_closure_call,
                "closure objects bind captured callbacks by activation context");
    }

    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "correlated-path-package";
        std::filesystem::create_directories(package_root);
        std::ofstream(package_root / "scenario.py")
            << "def live():\n"
               "    return True\n"
               "def dead():\n"
               "    return False\n"
               "def run(flag):\n"
               "    if flag and not flag:\n"
               "        return dead()\n"
               "    return live()\n";
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(package_root);
        const auto loaded = cpygraph::package::PackageLoader().load(compiled);
        const auto path_analysis =
            cpygraph::package::PackageAnalyzer().analyze(loaded);
        cpygraph::CodeObjectId run = 0U;
        cpygraph::CodeObjectId live = 0U;
        cpygraph::CodeObjectId dead = 0U;
        for (const auto& code : path_analysis.codeObjects()) {
            if (code.name == "run") run = code.id;
            if (code.name == "live") live = code.id;
            if (code.name == "dead") dead = code.id;
        }
        require(run != 0U && live != 0U && dead != 0U,
                "correlated-path fixture exposes every callable");
        cpygraph::PTASensitivityConfiguration complete;
        complete.level = cpygraph::PTASensitivityLevel::Complete;
        const auto graph = cpygraph::package::GraphBuilder().callGraph(
            path_analysis, {run}, complete);
        bool reaches_live = false;
        bool reaches_dead = false;
        for (const auto& edge : graph.graph.edges()) {
            if (edge.caller != run || !edge.target) continue;
            reaches_live |= edge.target->code == live;
            reaches_dead |= edge.target->code == dead;
        }
        require(reaches_live && !reaches_dead,
                "path sensitivity rejects contradictory tests of one stable local");
    }

    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "ddg-boundary-package";
        std::filesystem::create_directories(package_root);
        std::ofstream(package_root / "scenario.py")
            << "seed = object()\n"
               "class Box:\n"
               "    seed = object()\n"
               "def identity(value):\n"
               "    return value\n"
               "def read_seed():\n"
               "    return seed\n"
               "def run(value):\n"
               "    box = Box()\n"
               "    box.payload = identity(value)\n"
               "    return box.payload\n"
               "def replace(original, replacement):\n"
               "    box = Box()\n"
               "    box.payload = original\n"
               "    box.payload = replacement\n"
               "    return box.payload\n"
               "def aliased_receivers(left, right, first, second):\n"
               "    left.payload = first\n"
               "    right.payload = second\n"
               "    return left.payload\n"
               "result = run(seed)\n"
               "replaced = replace(seed, object())\n"
               "aliased = aliased_receivers(Box(), Box(), seed, object())\n"
               "observed = read_seed()\n";
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(package_root);
        const auto loaded_ddg =
            cpygraph::package::PackageLoader().load(compiled);
        const auto ddg_analysis =
            cpygraph::package::PackageAnalyzer().analyze(loaded_ddg);
        cpygraph::CodeObjectId module = 0U;
        cpygraph::CodeObjectId box_body = 0U;
        cpygraph::CodeObjectId identity = 0U;
        cpygraph::CodeObjectId read_seed = 0U;
        cpygraph::CodeObjectId replace = 0U;
        cpygraph::CodeObjectId aliased_receivers = 0U;
        cpygraph::CodeObjectId run = 0U;
        for (const auto& code : ddg_analysis.codeObjects()) {
            if (code.parent == 0U) module = code.id;
            if (code.name == "Box") box_body = code.id;
            if (code.name == "identity") identity = code.id;
            if (code.name == "read_seed") read_seed = code.id;
            if (code.name == "replace") replace = code.id;
            if (code.name == "aliased_receivers")
                aliased_receivers = code.id;
            if (code.name == "run") run = code.id;
        }
        require(module != 0U && box_body != 0U && identity != 0U &&
                    read_seed != 0U && replace != 0U &&
                    aliased_receivers != 0U && run != 0U,
                "DDG package fixture exposes each code boundary");
        const auto package_ddg =
            cpygraph::package::GraphBuilder().dataDependencyGraph(ddg_analysis);
        require(!package_ddg.call_boundaries.empty(),
                "package DDG exposes exact remapped call boundaries");
        require(!package_ddg.context_transitions.empty(),
                "package DDG exposes contextual interprocedural transitions");
        for (const auto& transition : package_ddg.context_transitions) {
            static_cast<void>(package_ddg.graph.node(transition.source));
            static_cast<void>(package_ddg.graph.node(transition.target));
            require(
                package_ddg.graph.hasEdge(
                    transition.source, transition.target,
                    transition.kind ==
                            cpygraph::package::
                                PackageDataDependencyGraphResult::
                                    ContextTransitionKind::ReturnToCall
                        ? cpygraph::ddg::EdgeKind::Result
                        : cpygraph::ddg::EdgeKind::DefUse),
                "every contextual transition identifies a stitched DDG edge");
        }
        for (const auto& boundary : package_ddg.call_boundaries) {
            const auto& call = package_ddg.graph.node(boundary.call);
            require(call.code == boundary.code,
                    "package DDG call boundary retains its code identity");
            for (const auto& argument : boundary.actual_arguments)
                for (const auto node : argument)
                    static_cast<void>(package_ddg.graph.node(node));
        }
        require(!package_ddg.heap_accesses.empty() &&
                    std::all_of(
                        package_ddg.heap_accesses.begin(),
                        package_ddg.heap_accesses.end(),
                        [&](const auto& access) {
                            return package_ddg.graph.node(access.node).code !=
                                0U;
                        }),
                "package DDG exposes exact remapped heap boundaries");
        cpygraph::package::PackageAnalysisObservations reused_observations;
        const auto reused_prerequisite =
            cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
                ddg_analysis, {}, &reused_observations);
        const auto observation_count = reused_observations.points_to.size();
        const auto reused_ddg =
            cpygraph::package::GraphBuilder().dataDependencyGraph(
                ddg_analysis, reused_prerequisite, reused_observations);
        require(reused_observations.points_to.size() == observation_count,
                "selective DDG reuse preserves the observation snapshot");
        require(reused_ddg.graph.nodes().size() ==
                    package_ddg.graph.nodes().size() &&
                reused_ddg.graph.edges().size() ==
                    package_ddg.graph.edges().size() &&
                reused_ddg.context_transitions.size() ==
                    package_ddg.context_transitions.size(),
                "selective DDG reuse preserves graph cardinality");
        for (const auto& edge : package_ddg.graph.edges())
            require(reused_ddg.graph.hasEdge(
                        edge.source, edge.target, edge.kind),
                    "selective DDG reuse preserves every dependency edge");
        const auto find_node = [&](cpygraph::CodeObjectId code,
                                   std::string_view label) {
            for (const auto& node : package_ddg.graph.nodes())
                if (node.code == code &&
                    package_ddg.graph.debugString(node.label) == label)
                    return node.id;
            return cpygraph::ddg::NodeId{};
        };
        const auto identity_formal = find_node(identity, "local value");
        const auto identity_return = find_node(identity, "return");
        const auto run_value = find_node(run, "load local value");
        cpygraph::ddg::NodeId run_call = 0U;
        for (const auto& node : package_ddg.graph.nodes())
            if (node.code == run &&
                package_ddg.graph.debugString(node.label) == "call" &&
                package_ddg.graph.hasEdge(
                    identity_return, node.id,
                    cpygraph::ddg::EdgeKind::Result))
                run_call = node.id;
        require(identity_formal != 0U && identity_return != 0U &&
                    run_value != 0U && run_call != 0U &&
                    package_ddg.graph.hasEdge(
                        run_value, identity_formal,
                        cpygraph::ddg::EdgeKind::DefUse) &&
                    package_ddg.graph.hasEdge(
                        identity_return, run_call,
                        cpygraph::ddg::EdgeKind::Result),
                "package DDG stitches exact actual/formal and return/call flows");

        const auto payload_store = find_node(run, "store attribute payload");
        const auto payload_load = find_node(run, "load attribute payload");
        require(payload_store != 0U && payload_load != 0U &&
                    package_ddg.graph.hasEdge(
                        payload_store, payload_load,
                        cpygraph::ddg::EdgeKind::DefUse),
                "package DDG uses PTA object identity to stitch heap def-use");

        std::vector<cpygraph::ddg::NodeId> replacement_stores;
        cpygraph::ddg::NodeId replacement_load = 0U;
        for (const auto& node : package_ddg.graph.nodes()) {
            if (node.code != replace) continue;
            const auto label = package_ddg.graph.debugString(node.label);
            if (label == "store attribute payload")
                replacement_stores.push_back(node.id);
            if (label == "load attribute payload")
                replacement_load = node.id;
        }
        std::sort(replacement_stores.begin(), replacement_stores.end(),
                  [&](const auto first, const auto second) {
                      return package_ddg.graph.node(first).bytecode_offset <
                             package_ddg.graph.node(second).bytecode_offset;
                  });
        constexpr std::size_t kReplacementStoreCount = 2U;
        require(replacement_stores.size() == kReplacementStoreCount &&
                    replacement_load != 0U &&
                    !package_ddg.graph.hasEdge(
                        replacement_stores.front(), replacement_load,
                        cpygraph::ddg::EdgeKind::DefUse) &&
                    package_ddg.graph.hasEdge(
                        replacement_stores.back(), replacement_load,
                        cpygraph::ddg::EdgeKind::DefUse),
                "package DDG strongly updates one unchanged receiver definition");

        std::vector<cpygraph::ddg::NodeId> aliased_stores;
        cpygraph::ddg::NodeId aliased_load = 0U;
        for (const auto& node : package_ddg.graph.nodes()) {
            if (node.code != aliased_receivers) continue;
            const auto label = package_ddg.graph.debugString(node.label);
            if (label == "store attribute payload")
                aliased_stores.push_back(node.id);
            if (label == "load attribute payload")
                aliased_load = node.id;
        }
        constexpr std::size_t kAliasedStoreCount = 2U;
        require(
            aliased_stores.size() == kAliasedStoreCount &&
                aliased_load != 0U &&
                std::all_of(
                    aliased_stores.begin(), aliased_stores.end(),
                    [&](const auto store) {
                        return package_ddg.graph.hasEdge(
                            store, aliased_load,
                            cpygraph::ddg::EdgeKind::DefUse);
                    }),
            "package DDG keeps weak updates for distinct receiver definitions "
            "that share one abstract class-instance object");

        const auto module_seed = find_node(module, "store global seed");
        const auto class_seed = find_node(box_body, "store global seed");
        const auto read_seed_load = find_node(read_seed, "load global seed");
        require(module_seed != 0U && class_seed != 0U &&
                    read_seed_load != 0U &&
                    package_ddg.graph.hasEdge(
                        module_seed, read_seed_load,
                        cpygraph::ddg::EdgeKind::DefUse) &&
                    !package_ddg.graph.hasEdge(
                        class_seed, read_seed_load,
                        cpygraph::ddg::EdgeKind::DefUse),
                "package DDG stitches module globals without merging class namespaces");
    }

    std::filesystem::path compiled_bytecode;
    cpygraph::package::LoadedPackage loaded({}, {}, {});
    {
        const auto compiled = cpygraph::package::PackageCompiler().compile(
            CPYGRAPH_TEST_PACKAGE_PATH);
        require(compiled.modules().size() == kExpectedModuleCount,
                "package compiler discovers every Python module");
        compiled_bytecode = compiled.modules().front().bytecode_path;
        require(std::filesystem::is_regular_file(compiled_bytecode),
                "package compiler materializes native bytecode");
        loaded = cpygraph::package::PackageLoader().load(compiled);
    }
    require(!std::filesystem::exists(compiled_bytecode),
            "compiled package removes its temporary bytecode with RAII");
    require(loaded.modules().size() == kExpectedModuleCount,
            "package loader preserves all modules");
    require(loaded.modules().front().name == kPackageModuleName,
            "package initializer receives its importable package name");
    require(loaded.modules().back().name == kWorkflowModuleName,
            "ordinary module receives its fully-qualified name");

    const auto analysis = cpygraph::package::PackageAnalyzer().analyze(loaded);
    require(analysis.moduleCount() == kExpectedModuleCount,
            "package analysis reports module count");
    require(analysis.codeObjects().size() == kExpectedCodeObjectCount,
            "package analysis includes nested code objects");
    bool found_nested_function = false;
    for (const auto& code : analysis.codeObjects()) {
        require(!code.cfg.blocks().empty(), "every loaded code object has a CFG");
        if (code.name == kNestedFunctionName && code.parent != 0U)
            found_nested_function = true;
    }
    require(found_nested_function,
            "nested function retains its parent code-object identity");

    const auto call_graph =
        cpygraph::package::GraphBuilder().callGraph(analysis);
    require(call_graph.analyzed_code_object_count ==
                kExpectedPTAAnalyzedCodeObjectCount,
            "on-the-fly PTA does not analyze an unreachable function body");
    require(call_graph.analyzed_code_object_count < analysis.codeObjects().size(),
            "package CG distinguishes discovered and PTA-reachable code objects");
    require(call_graph.points_to_coverage ==
                cpygraph::package::SoundnessCoverage::ConservativeTop &&
                call_graph.call_graph_coverage ==
                cpygraph::package::SoundnessCoverage::ConservativeTop,
            "package PTA/CG expose compact conservative-top soundness summaries");
    cpygraph::CodeObjectId imported_code = 0;
    cpygraph::CodeObjectId identity_code = 0;
    cpygraph::CodeObjectId run_code = 0;
    for (const auto& code : analysis.codeObjects()) {
        if (code.name == kImportedFunctionName) imported_code = code.id;
        if (code.name == kNestedFunctionName) identity_code = code.id;
        if (code.name == kRunFunctionName) run_code = code.id;
    }
    std::size_t resolved_import_edges = 0;
    for (const auto& edge : call_graph.graph.edges()) {
        if (edge.target && edge.target->code == imported_code) {
            ++resolved_import_edges;
        }
    }
    require(imported_code != 0U &&
                resolved_import_edges >= kExpectedImportedCallEdges,
            "relative and dotted imports resolve exports through module identity");

    cpygraph::package::PackageAnalysisObservations observations;
    const auto observed_call_graph =
        cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
            analysis, {}, &observations);
    require(observations.points_to.size() >= kMinimumObservationCount &&
                observations.object_origins.size() >= kMinimumObservationCount &&
                observations.call_sites.size() >= kMinimumObservationCount,
            "optional package observation snapshot exposes numeric PTA/CG identities");
    require(std::all_of(
                observations.call_sites.begin(), observations.call_sites.end(),
                [](const auto& observation) {
                    return observation.callee_value != 0U &&
                           observation.result_value != 0U &&
                           std::all_of(
                               observation.callee_inputs.begin(),
                               observation.callee_inputs.end(),
                               [](cpygraph::NodeId value) {
                                   return value != 0U;
                               }) &&
                           observation.explicit_argument_offset <=
                               observation.arguments.size() &&
                           std::all_of(
                               observation.arguments.begin(),
                               observation.arguments.end(),
                               [](const auto& alternatives) {
                                   return !alternatives.empty() &&
                                          std::all_of(
                                              alternatives.begin(),
                                              alternatives.end(),
                                              [](cpygraph::NodeId value) {
                                                  return value != 0U;
                                              });
                               });
                }),
            "call observations expose result, callee inputs, and normalized arguments");
    require(!observations.callable_objects.empty() &&
                std::all_of(
                    observations.callable_objects.begin(),
                    observations.callable_objects.end(),
                    [](const auto& callable) {
                        return callable.object != 0U &&
                               callable.function != 0U &&
                               callable.code != 0U &&
                               callable.defining_code != 0U;
                    }),
            "callable observations preserve object, function, code, and definition identities");
    require(std::all_of(observations.points_to.begin(), observations.points_to.end(),
                        [](const auto& observation) {
                            return observation.context == 0U;
                        }),
            "default package PTA is context and path insensitive");
    const auto relative_import = std::find_if(
        observations.import_requests.begin(), observations.import_requests.end(),
        [](const auto& request) {
            return request.module == "sample_package.helper" &&
                   request.from_names == std::vector<std::string>{"imported"};
        });
    require(relative_import != observations.import_requests.end() &&
                relative_import->source_context == 0U &&
                relative_import->relative_name_valid,
            "package observations expose resolved relative imports and fromlists");
    for (const auto& edge : observed_call_graph.graph.edges())
        require(std::any_of(observations.call_sites.begin(), observations.call_sites.end(),
                            [&](const auto& site) { return site.site == edge.site; }),
                "every observed call edge retains its semantic instruction site");

    cpygraph::PTASensitivityConfiguration selective_sensitivity;
    selective_sensitivity.level = cpygraph::PTASensitivityLevel::Selective;
    selective_sensitivity.functions = {
        {identity_code, cpygraph::PTASensitivity::Context},
        {run_code, cpygraph::PTASensitivity::Path},
    };
    cpygraph::package::PackageAnalysisObservations selective_observations;
    const auto selective_call_graph =
        cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
            analysis, {}, &selective_observations, selective_sensitivity);
    std::unordered_set<cpygraph::PTAContextId> identity_contexts;
    std::unordered_set<cpygraph::PTAContextId> run_contexts;
    std::unordered_map<cpygraph::PTAContextId,
                       std::unordered_set<cpygraph::ObjectId>>
        identity_returns;
    std::size_t identity_load_index = 0U;
    const auto& identity_program = analysis.codeObject(identity_code).cfg.program();
    for (std::size_t index = 0; index < identity_program.size(); ++index)
        if (identity_program[index].opcode ==
            cpygraph::bytecode::SemanticOpcode::LoadLocal)
            identity_load_index = index;
    for (const auto& observation : selective_observations.points_to) {
        if (observation.code == identity_code) {
            identity_contexts.insert(observation.context);
            if (observation.instruction_index == identity_load_index)
                identity_returns[observation.context].insert(
                    observation.objects.begin(), observation.objects.end());
        }
        if (observation.code == run_code)
            run_contexts.insert(observation.context);
        if (observation.code != identity_code && observation.code != run_code)
            require(observation.context == 0U,
                    "selective PTA leaves unconfigured functions insensitive");
    }
    require(identity_contexts.size() == 2U &&
                identity_contexts.count(0U) == 0U,
            "context-selected function receives one PTA context per static call site");
    require(run_contexts.size() == 2U && run_contexts.count(0U) == 0U,
            "path-selected function receives one PTA context per acyclic branch path");
    require(identity_returns.size() == 2U,
            "context-selected return values remain queryable per PTA context");
    const auto first_return = identity_returns.begin();
    const auto second_return = std::next(first_return);
    bool shared_return_object = false;
    for (const auto object : first_return->second)
        shared_return_object |= second_return->second.count(object) != 0U;
    require(!shared_return_object,
            "context-sensitive PTA prevents values from distinct call sites from merging");
    std::unordered_map<cpygraph::PTAContextId, std::size_t> run_call_sites;
    for (const auto& site : selective_observations.call_sites)
        if (site.code == run_code &&
            analysis.codeObject(run_code).cfg.program()[site.instruction_index].opcode ==
                cpygraph::bytecode::SemanticOpcode::Call)
            ++run_call_sites[site.context];
    require(run_call_sites.size() == 2U &&
                std::all_of(run_call_sites.begin(), run_call_sites.end(),
                            [](const auto& entry) {
                                return entry.second == 1U;
                            }),
            "each path-sensitive PTA context contains only its feasible call site");
    require(selective_call_graph.analyzed_code_object_count ==
                observed_call_graph.analyzed_code_object_count,
            "selective PTA sensitivity preserves the reachable code-object set");

    cpygraph::PTASensitivityConfiguration complete_sensitivity;
    complete_sensitivity.level = cpygraph::PTASensitivityLevel::Complete;
    cpygraph::package::PackageAnalysisObservations complete_observations;
    cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
        analysis, {}, &complete_observations, complete_sensitivity);
    require(std::all_of(
                complete_observations.points_to.begin(),
                complete_observations.points_to.end(),
                [](const auto& observation) {
                    return observation.context != 0U;
                }),
            "complete PTA sensitivity applies path contexts to every function");

    cpygraph::PTASensitivityConfiguration invalid_sensitivity;
    invalid_sensitivity.functions.push_back(
        {identity_code, cpygraph::PTASensitivity::Flow});
    bool rejected_mismatched_sensitivity = false;
    try {
        cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
            analysis, {}, nullptr, invalid_sensitivity);
    } catch (const std::invalid_argument&) {
        rejected_mismatched_sensitivity = true;
    }
    require(rejected_mismatched_sensitivity,
            "function selections require the selective PTA policy level");

    cpygraph::PTASensitivityConfiguration invalid_complete_sensitivity;
    invalid_complete_sensitivity.level =
        cpygraph::PTASensitivityLevel::Complete;
    invalid_complete_sensitivity.functions.push_back(
        {identity_code, cpygraph::PTASensitivity::Flow});
    bool rejected_complete_function_list = false;
    try {
        cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
            analysis, {}, nullptr, invalid_complete_sensitivity);
    } catch (const std::invalid_argument&) {
        rejected_complete_function_list = true;
    }
    require(rejected_complete_function_list,
            "complete PTA sensitivity applies globally without a function list");

    {
        TemporaryPackageTree tree;
        const auto package_root = tree.root() / "context-import-package";
        std::filesystem::create_directories(package_root);
        std::ofstream(package_root / "scenario.py")
            << "def load_dependency():\n"
               "    import context_external_dependency\n"
               "load_dependency()\n"
               "load_dependency()\n";
        const auto compiled =
            cpygraph::package::PackageCompiler().compile(package_root);
        const auto loaded = cpygraph::package::PackageLoader().load(compiled);
        const auto import_analysis =
            cpygraph::package::PackageAnalyzer().analyze(loaded);
        cpygraph::CodeObjectId load_dependency = 0U;
        for (const auto& code : import_analysis.codeObjects())
            if (code.name == "load_dependency") load_dependency = code.id;
        require(load_dependency != 0U,
                "context-import fixture exposes its callable code object");

        cpygraph::PTASensitivityConfiguration import_sensitivity;
        import_sensitivity.level =
            cpygraph::PTASensitivityLevel::Selective;
        import_sensitivity.functions = {{
            load_dependency, cpygraph::PTASensitivity::Context}};
        cpygraph::package::PackageAnalysisObservations import_observations;
        cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
            import_analysis, {}, &import_observations, import_sensitivity);
        std::unordered_set<cpygraph::PTAContextId> import_contexts;
        for (const auto& request : import_observations.import_requests)
            if (request.source == load_dependency &&
                request.module == "context_external_dependency")
                import_contexts.insert(request.source_context);
        require(import_contexts.size() == 2U &&
                    import_contexts.count(0U) == 0U,
                "import observations retain each activated PTA context");
    }

    const auto control_flow =
        cpygraph::package::GraphBuilder().controlFlowGraphs(analysis);
    require(control_flow.points_to.analyzed_code_object_count ==
                kExpectedPTAAnalyzedCodeObjectCount,
            "package CFG API completes its PTA prerequisite");
    require(control_flow.graphs.size() == kExpectedCodeObjectCount,
            "package CFG API returns each discovered code object's graph");
    require(control_flow.coverage ==
                cpygraph::package::SoundnessCoverage::TypedUnresolved,
            "package CFG scopes unresolved control transfers by semantic query");

    const auto control_dependency =
        cpygraph::package::GraphBuilder().controlDependencyGraphs(analysis);
    require(control_dependency.points_to.analyzed_code_object_count ==
                kExpectedPTAAnalyzedCodeObjectCount,
            "package CDG API completes its PTA prerequisite");
    require(control_dependency.graphs.size() == kExpectedCodeObjectCount,
            "package CDG API returns each discovered code object's graph");
    require(control_dependency.coverage ==
                cpygraph::package::SoundnessCoverage::TypedUnresolved,
            "package CDG scopes unresolved control dependence by semantic query");

    const auto data_dependency =
        cpygraph::package::GraphBuilder().dataDependencyGraph(analysis);
    require(data_dependency.coverage ==
                cpygraph::package::SoundnessCoverage::TypedUnresolved,
            "package DDG scopes unresolved dependencies by language feature");

    cpygraph::package::PackageAnalysisObservations pipeline_observations;
    const auto pipeline = cpygraph::package::GraphBuilder().analyze(
        analysis, {}, {}, &pipeline_observations);
    require(pipeline.call_graph.analyzed_code_object_count ==
                kExpectedPTAAnalyzedCodeObjectCount &&
                pipeline.control_flow.graphs.size() ==
                    kExpectedCodeObjectCount &&
                pipeline.control_dependency.graphs.size() ==
                    kExpectedCodeObjectCount &&
                !pipeline.data_dependency.graph.nodes().empty() &&
                !pipeline_observations.points_to.empty(),
            "coupled package API materializes all products from one PTA result");
    require(pipeline.call_graph.points_to_statistics.value_count != 0U &&
                pipeline.call_graph.points_to_statistics.object_count != 0U &&
                pipeline.call_graph.points_to_statistics.constraint_count != 0U &&
                pipeline.call_graph.points_to_statistics.solver_iteration_count != 0U,
            "coupled package API retains PTA scale measurements");
}
