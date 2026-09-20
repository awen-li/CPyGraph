#include "package/package.h"

#include "analysis/profile/profiler.h"

#include <Python.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

#ifndef CPYGRAPH_PYTHON_EXECUTABLE
#define CPYGRAPH_PYTHON_EXECUTABLE "python3"
#endif

namespace cpygraph::package {
namespace {

constexpr char kSourceExtension[] = ".py";
constexpr char kBytecodeExtension[] = ".pyc";
constexpr char kPackageInitializer[] = "__init__";
constexpr char kSourceLayoutDirectory[] = "src";
constexpr char kCompileScript[] =
    "import py_compile,sys; py_compile.compile(sys.argv[1], cfile=sys.argv[2], doraise=True)";
constexpr char kVersionCheckScript[] =
    "import sys; raise SystemExit(0 if sys.version_info[:2] == (int(sys.argv[1]), int(sys.argv[2])) else int(sys.argv[3]))";
constexpr int kVersionMismatchExitCode = 64;
constexpr int kSignalExitStatusBase = 128;
constexpr int kAbnormalProcessExitStatus = 125;
constexpr std::size_t kPythonMajorVersion = PY_MAJOR_VERSION;
constexpr std::size_t kPythonMinorVersion = PY_MINOR_VERSION;

std::string runtimeVersion() {
    return std::to_string(kPythonMajorVersion) + "." +
           std::to_string(kPythonMinorVersion);
}

int runProcess(const std::vector<std::string>& arguments) {
    if (arguments.empty()) throw std::invalid_argument("process arguments cannot be empty");
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1U);
    for (const auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t child{};
    const auto spawn_error = ::posix_spawnp(&child, argv.front(), nullptr, nullptr,
                                            argv.data(), environ);
    if (spawn_error != 0)
        throw std::system_error(spawn_error, std::generic_category(),
                                "cannot start Python compiler");

    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            throw std::system_error(errno, std::generic_category(),
                                    "cannot wait for Python compiler");
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return kSignalExitStatusBase + WTERMSIG(status);
    return kAbnormalProcessExitStatus;
}

std::filesystem::path makeTemporaryDirectory() {
    auto pattern = (std::filesystem::temp_directory_path() /
                    "cpygraph-package-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const auto* created = ::mkdtemp(writable.data());
    if (!created)
        throw std::system_error(errno, std::generic_category(),
                                "cannot create package compilation directory");
    return created;
}

bool containsPythonSource(const std::filesystem::path& directory) {
    if (!std::filesystem::is_directory(directory)) return false;
    return std::any_of(
        std::filesystem::recursive_directory_iterator(directory),
        std::filesystem::recursive_directory_iterator(),
        [](const auto& entry) {
            return entry.is_regular_file() &&
                   entry.path().extension() == kSourceExtension;
        });
}

std::filesystem::path selectSourceRoot(const std::filesystem::path& input) {
    if (std::filesystem::is_regular_file(input)) return input.parent_path();
    const auto src = input / kSourceLayoutDirectory;
    if (containsPythonSource(src)) return src;
    return input;
}

std::string moduleName(const std::filesystem::path& source,
                       const std::filesystem::path& source_root,
                       bool root_is_package) {
    auto relative = source.lexically_relative(source_root);
    relative.replace_extension();
    std::vector<std::string> parts;
    if (root_is_package) parts.push_back(source_root.filename().string());
    for (const auto& part : relative) parts.push_back(part.string());
    if (!parts.empty() && parts.back() == kPackageInitializer) parts.pop_back();
    if (parts.empty()) parts.push_back(source_root.filename().string());

    std::string result;
    for (const auto& part : parts) {
        if (!result.empty()) result.push_back('.');
        result += part;
    }
    return result;
}

std::vector<std::filesystem::path> discoverSources(
    const std::filesystem::path& input,
    const std::filesystem::path& source_root) {
    const auto require_contained = [&](const std::filesystem::path& source) {
        const auto resolved = std::filesystem::canonical(source);
        const auto relative = resolved.lexically_relative(source_root);
        const auto escapes = relative.empty() || relative.is_absolute() ||
            std::any_of(relative.begin(), relative.end(), [](const auto& part) {
                return part == "..";
            });
        if (escapes)
            throw std::invalid_argument(
                "package source resolves outside the selected source root: " +
                source.string());
    };
    std::vector<std::filesystem::path> result;
    if (std::filesystem::is_regular_file(input)) {
        if (input.extension() != kSourceExtension)
            throw std::invalid_argument("package input file must have a .py extension: " +
                                        input.string());
        const auto source = std::filesystem::absolute(input).lexically_normal();
        require_contained(source);
        result.push_back(source);
        return result;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source_root)) {
        if (!entry.is_regular_file() || entry.path().extension() != kSourceExtension) continue;
        const auto source = std::filesystem::absolute(entry.path()).lexically_normal();
        require_contained(source);
        result.push_back(source);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void ensureMatchingInterpreter(const std::filesystem::path& executable) {
    const auto status = runProcess({executable.string(), "-c", kVersionCheckScript,
                                    std::to_string(kPythonMajorVersion),
                                    std::to_string(kPythonMinorVersion),
                                    std::to_string(kVersionMismatchExitCode)});
    if (status == kVersionMismatchExitCode)
        throw std::runtime_error("Python compiler must match CPyGraph's linked CPython " +
                                 runtimeVersion());
    if (status != EXIT_SUCCESS)
        throw std::runtime_error("cannot execute Python compiler " + executable.string() +
                                 " (exit status " + std::to_string(status) + ")");
}

void assignModuleName(bytecode::LoadedCodeObject& code, const std::string& module_name) {
    code.module_name = module_name;
    for (auto& child : code.nested) assignModuleName(child, module_name);
}

std::string bytecodeModuleStem(const std::filesystem::path& bytecode_path) {
    constexpr char kCPythonCacheTagMarker[] = ".cpython-";
    constexpr char kPyPyCacheTagMarker[] = ".pypy";
    auto name = bytecode_path.stem().string();
    const auto cpython_tag = name.find(kCPythonCacheTagMarker);
    const auto pypy_tag = name.find(kPyPyCacheTagMarker);
    const auto tag = std::min(cpython_tag, pypy_tag);
    if (tag != std::string::npos) name.resize(tag);
    return name;
}

std::string bytecodeModuleName(const std::filesystem::path& bytecode_path) {
    auto name = bytecodeModuleStem(bytecode_path);
    if (name == kPackageInitializer) {
        auto directory = bytecode_path.parent_path();
        constexpr char kBytecodeCacheDirectory[] = "__pycache__";
        if (directory.filename() == kBytecodeCacheDirectory)
            directory = directory.parent_path();
        if (!directory.filename().empty()) return directory.filename().string();
    }
    return name;
}

}  // namespace

CompiledPackage::CompiledPackage(std::filesystem::path source_root,
                                 std::filesystem::path temporary_root,
                                 std::vector<CompiledModule> modules,
                                 std::string python_version)
    : source_root_(std::move(source_root)), temporary_root_(std::move(temporary_root)),
      modules_(std::move(modules)), python_version_(std::move(python_version)) {}

CompiledPackage::CompiledPackage(CompiledPackage&& other) noexcept
    : source_root_(std::move(other.source_root_)),
      temporary_root_(std::move(other.temporary_root_)),
      modules_(std::move(other.modules_)),
      python_version_(std::move(other.python_version_)) {
    other.temporary_root_.clear();
}

CompiledPackage& CompiledPackage::operator=(CompiledPackage&& other) noexcept {
    if (this == &other) return *this;
    cleanup();
    source_root_ = std::move(other.source_root_);
    temporary_root_ = std::move(other.temporary_root_);
    modules_ = std::move(other.modules_);
    python_version_ = std::move(other.python_version_);
    other.temporary_root_.clear();
    return *this;
}

CompiledPackage::~CompiledPackage() { cleanup(); }

void CompiledPackage::cleanup() noexcept {
    if (temporary_root_.empty()) return;
    std::error_code error;
    std::filesystem::remove_all(temporary_root_, error);
    temporary_root_.clear();
}

PackageCompiler::PackageCompiler(std::filesystem::path python_executable)
    : python_executable_(python_executable.empty()
                             ? std::filesystem::path(CPYGRAPH_PYTHON_EXECUTABLE)
                             : std::move(python_executable)) {}

CompiledPackage PackageCompiler::compile(
    const std::filesystem::path& package_path) const {
    profile::Scope profile_scope(profile::Phase::PackageCompilation);
    if (!std::filesystem::exists(package_path))
        throw std::invalid_argument("package path does not exist: " + package_path.string());
    if (!std::filesystem::is_directory(package_path) &&
        !std::filesystem::is_regular_file(package_path))
        throw std::invalid_argument("package path must be a directory or Python source: " +
                                    package_path.string());

    ensureMatchingInterpreter(python_executable_);
    const auto canonical_input = std::filesystem::canonical(package_path);
    const auto source_root = std::filesystem::canonical(selectSourceRoot(canonical_input));
    const auto sources = discoverSources(canonical_input, source_root);
    if (sources.empty())
        throw std::invalid_argument("package contains no Python source files: " +
                                    package_path.string());

    const auto temporary_root = makeTemporaryDirectory();
    try {
        const bool root_is_package = std::filesystem::is_directory(canonical_input) &&
            std::filesystem::is_regular_file(source_root /
                                              (std::string(kPackageInitializer) +
                                               kSourceExtension));
        std::vector<CompiledModule> modules;
        modules.reserve(sources.size());
        for (const auto& source : sources) {
            auto relative = source.lexically_relative(source_root);
            auto bytecode = temporary_root / relative;
            bytecode.replace_extension(kBytecodeExtension);
            std::filesystem::create_directories(bytecode.parent_path());
            const auto status = runProcess({python_executable_.string(), "-c", kCompileScript,
                                            source.string(), bytecode.string()});
            if (status != EXIT_SUCCESS)
                throw std::runtime_error("failed to compile " + source.string() +
                                         " (exit status " + std::to_string(status) + ")");
            modules.push_back({moduleName(source, source_root, root_is_package), source,
                               bytecode, source.stem() == kPackageInitializer});
        }
        return CompiledPackage(source_root, temporary_root, std::move(modules),
                               runtimeVersion());
    } catch (...) {
        std::error_code error;
        std::filesystem::remove_all(temporary_root, error);
        throw;
    }
}

LoadedPackage::LoadedPackage(std::filesystem::path source_root,
                             std::vector<LoadedModule> modules,
                             std::string python_version)
    : source_root_(std::move(source_root)), modules_(std::move(modules)),
      python_version_(std::move(python_version)) {}

LoadedPackage PackageLoader::load(const CompiledPackage& package) const {
    profile::Scope profile_scope(profile::Phase::PackageLoading);
    if (!Py_IsInitialized()) Py_Initialize();
    auto loader = bytecode::CodeObjectLoaderFactory::forVersion(package.pythonVersion());
    std::vector<LoadedModule> modules;
    modules.reserve(package.modules().size());
    for (const auto& compiled : package.modules()) {
        auto code = loader->load(compiled.bytecode_path);
        assignModuleName(code, compiled.name);
        modules.push_back({compiled.name, compiled.source_path, compiled.is_package,
                           std::move(code)});
    }
    return LoadedPackage(package.sourceRoot(), std::move(modules),
                         std::string(package.pythonVersion()));
}

LoadedPackage PackageLoader::loadBytecode(
    const std::filesystem::path& bytecode_path) const {
    profile::Scope profile_scope(profile::Phase::PackageLoading);
    if (!std::filesystem::exists(bytecode_path))
        throw std::invalid_argument("bytecode path does not exist: " +
                                    bytecode_path.string());
    if (!std::filesystem::is_regular_file(bytecode_path) ||
        bytecode_path.extension() != kBytecodeExtension)
        throw std::invalid_argument("bytecode input must be a .pyc file: " +
                                    bytecode_path.string());

    const auto canonical_path = std::filesystem::canonical(bytecode_path);
    if (!Py_IsInitialized()) Py_Initialize();
    auto loader = bytecode::CodeObjectLoaderFactory::forCurrentRuntime();
    const auto module_name = bytecodeModuleName(canonical_path);
    auto code = loader->load(canonical_path);
    assignModuleName(code, module_name);
    const auto is_package =
        bytecodeModuleStem(canonical_path) == kPackageInitializer;
    auto source_root = canonical_path.parent_path();
    constexpr char kBytecodeCacheDirectory[] = "__pycache__";
    if (source_root.filename() == kBytecodeCacheDirectory)
        source_root = source_root.parent_path();
    std::vector<LoadedModule> modules;
    modules.push_back({module_name, canonical_path, is_package, std::move(code)});
    return LoadedPackage(std::move(source_root), std::move(modules),
                         runtimeVersion());
}

}  // namespace cpygraph::package
