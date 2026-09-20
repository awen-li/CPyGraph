#pragma once

#include "bytecode/code_object_loader.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cpygraph::package {

struct CompiledModule {
    std::string name;
    std::filesystem::path source_path;
    std::filesystem::path bytecode_path;
    bool is_package{false};
};

// Owns the temporary bytecode files produced for one package. The files stay
// valid until this object is destroyed, making the compile/load boundary
// explicit without leaking build artifacts into the source tree.
class CompiledPackage {
public:
    CompiledPackage(CompiledPackage&& other) noexcept;
    CompiledPackage& operator=(CompiledPackage&& other) noexcept;
    ~CompiledPackage();

    CompiledPackage(const CompiledPackage&) = delete;
    CompiledPackage& operator=(const CompiledPackage&) = delete;

    const std::filesystem::path& sourceRoot() const noexcept { return source_root_; }
    const std::vector<CompiledModule>& modules() const noexcept { return modules_; }
    std::string_view pythonVersion() const noexcept { return python_version_; }

private:
    friend class PackageCompiler;
    CompiledPackage(std::filesystem::path source_root,
                    std::filesystem::path temporary_root,
                    std::vector<CompiledModule> modules,
                    std::string python_version);

    void cleanup() noexcept;

    std::filesystem::path source_root_;
    std::filesystem::path temporary_root_;
    std::vector<CompiledModule> modules_;
    std::string python_version_;
};

class PackageCompiler {
public:
    // An empty executable selects the interpreter CPyGraph was built with.
    explicit PackageCompiler(std::filesystem::path python_executable = {});

    CompiledPackage compile(const std::filesystem::path& package_path) const;
    const std::filesystem::path& pythonExecutable() const noexcept {
        return python_executable_;
    }

private:
    std::filesystem::path python_executable_;
};

struct LoadedModule {
    std::string name;
    std::filesystem::path source_path;
    bool is_package{false};
    bytecode::LoadedCodeObject code;
};

class LoadedPackage {
public:
    LoadedPackage(std::filesystem::path source_root,
                  std::vector<LoadedModule> modules,
                  std::string python_version);

    const std::filesystem::path& sourceRoot() const noexcept { return source_root_; }
    const std::vector<LoadedModule>& modules() const noexcept { return modules_; }
    std::string_view pythonVersion() const noexcept { return python_version_; }

private:
    std::filesystem::path source_root_;
    std::vector<LoadedModule> modules_;
    std::string python_version_;
};

class PackageLoader {
public:
    LoadedPackage load(const CompiledPackage& package) const;
    // Decode one native .pyc artifact directly. No source discovery or
    // compilation occurs, and the file must match the linked CPython runtime.
    LoadedPackage loadBytecode(
        const std::filesystem::path& bytecode_path) const;
};

}  // namespace cpygraph::package
