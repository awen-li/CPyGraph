#pragma once

#include "analysis/cfg/control_flow_graph.h"
#include "common/model.h"
#include "package/package.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cpygraph::package {

struct CodeObjectAnalysis {
    CodeObjectId id{};
    CodeObjectId parent{};
    std::string module_name;
    std::string name;
    std::string qualname;
    std::filesystem::path filename;
    bool module_is_package{false};
    std::uint32_t argument_count{};
    std::uint32_t positional_only_argument_count{};
    std::uint32_t keyword_only_argument_count{};
    bool has_var_arguments{false};
    bool has_var_keywords{false};
    std::vector<std::string> local_names;
    // Indexed by co_consts operand. Zero means the constant is not code.
    std::vector<CodeObjectId> constant_code_objects;
    cfg::ControlFlowGraph cfg;
    std::vector<std::string> free_names;
};

class PackageAnalysis {
public:
    PackageAnalysis(std::filesystem::path source_root, std::string python_version,
                    std::size_t module_count,
                    std::vector<CodeObjectAnalysis> code_objects);

    const std::filesystem::path& sourceRoot() const noexcept { return source_root_; }
    std::string_view pythonVersion() const noexcept { return python_version_; }
    std::size_t moduleCount() const noexcept { return module_count_; }
    const std::vector<CodeObjectAnalysis>& codeObjects() const noexcept {
        return code_objects_;
    }
    const CodeObjectAnalysis& codeObject(CodeObjectId id) const;

private:
    std::filesystem::path source_root_;
    std::string python_version_;
    std::size_t module_count_{};
    std::vector<CodeObjectAnalysis> code_objects_;
};

class PackageAnalyzer {
public:
    PackageAnalysis analyze(const LoadedPackage& package) const;
};

}  // namespace cpygraph::package
