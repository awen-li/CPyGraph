#pragma once

#include "bytecode/semantic_ir.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cpygraph::bytecode {

struct LoadedConstant {
    enum class Kind { Other, CodeObject };
    Kind kind{Kind::Other};
    std::string label;
    std::size_t nested_code_index{};
    // Semantic literal metadata is kept outside graph attributes. Adapters
    // and core analyses may use it without depending on debug labels.
    std::optional<std::string> string_value;
};

struct LoadedCodeObject {
    std::string module_name;
    std::string name;
    std::string qualname;
    std::string filename;
    std::uint32_t argument_count{};
    std::uint32_t positional_only_argument_count{};
    std::uint32_t keyword_only_argument_count{};
    bool has_var_arguments{false};
    bool has_var_keywords{false};
    std::uint32_t local_count{};
    std::vector<std::uint8_t> bytecode;
    CodeMetadata metadata;
    std::vector<LoadedConstant> constants;
    std::vector<LoadedCodeObject> nested;
};

class CodeObjectLoader {
public:
    virtual ~CodeObjectLoader() = default;
    virtual std::string_view version() const noexcept = 0;
    virtual LoadedCodeObject load(const std::filesystem::path& path) const = 0;
};

class CodeObjectLoaderFactory {
public:
    static std::unique_ptr<CodeObjectLoader> forVersion(std::string_view version);
    static std::unique_ptr<CodeObjectLoader> forCurrentRuntime();
};

}  // namespace cpygraph::bytecode
