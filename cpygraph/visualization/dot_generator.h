#pragma once

#include "analysis/cdg/control_dependency_graph.h"
#include "analysis/cfg/control_flow_graph.h"
#include "analysis/cg/call_graph.h"
#include "analysis/ddg/data_dependency_graph.h"

#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>

namespace cpygraph::visualization {

// Template-method base for Graphviz DOT output. It owns document framing,
// escaping, and file I/O; derived generators emit only graph-specific content.
class DotGenerator {
public:
    virtual ~DotGenerator() = default;

    std::string generate() const;
    void write(const std::filesystem::path& output_path) const;

protected:
    virtual std::string_view graphName() const noexcept = 0;
    virtual void emitBody(std::ostream& output) const = 0;

    static std::string quote(std::string_view text);
    static void emitNode(std::ostream& output, std::string_view id,
                         std::string_view label,
                         std::string_view attributes = {});
    static void emitEdge(std::ostream& output, std::string_view source,
                         std::string_view target, std::string_view label,
                         std::string_view attributes = {});
};

class CFGDotGenerator final : public DotGenerator {
public:
    explicit CFGDotGenerator(const cfg::ControlFlowGraph& graph) noexcept
        : graph_(graph) {}

protected:
    std::string_view graphName() const noexcept override;
    void emitBody(std::ostream& output) const override;

private:
    const cfg::ControlFlowGraph& graph_;
};

class CDGDotGenerator final : public DotGenerator {
public:
    explicit CDGDotGenerator(const cdg::ControlDependencyGraph& graph) noexcept
        : graph_(graph) {}

protected:
    std::string_view graphName() const noexcept override;
    void emitBody(std::ostream& output) const override;

private:
    const cdg::ControlDependencyGraph& graph_;
};

class CallGraphDotGenerator final : public DotGenerator {
public:
    explicit CallGraphDotGenerator(const cg::CallGraph& graph) noexcept
        : graph_(graph) {}

protected:
    std::string_view graphName() const noexcept override;
    void emitBody(std::ostream& output) const override;

private:
    const cg::CallGraph& graph_;
};

class DDGDotGenerator final : public DotGenerator {
public:
    explicit DDGDotGenerator(const ddg::DataDependencyGraph& graph) noexcept
        : graph_(graph) {}

protected:
    std::string_view graphName() const noexcept override;
    void emitBody(std::ostream& output) const override;

private:
    const ddg::DataDependencyGraph& graph_;
};

}  // namespace cpygraph::visualization
