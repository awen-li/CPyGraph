#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace cpygraph::profile {

enum class Phase : std::uint8_t {
    EndToEnd,
    PackageCompilation,
    PackageLoading,
    BytecodeAnalysis,
    PointsToCallGraph,
    ControlFlowGraph,
    ControlDependencyGraph,
    DataDependencyGraph,
    Count,
};

enum class Status : std::uint8_t {
    Success,
    Failed,
};

using Token = std::uint64_t;

constexpr Token kInvalidToken = 0U;
constexpr std::size_t kPhaseCount = static_cast<std::size_t>(Phase::Count);
constexpr auto kDefaultSampleInterval = std::chrono::milliseconds(5);

struct Record {
    Phase phase{Phase::EndToEnd};
    Status status{Status::Success};
    std::uint64_t wall_time_ns{};
    std::uint64_t cpu_time_ns{};
    std::uint64_t rss_start_bytes{};
    std::uint64_t rss_end_bytes{};
    std::uint64_t peak_rss_bytes{};
    std::int64_t rss_delta_bytes{};
};

struct Aggregate {
    Phase phase{Phase::EndToEnd};
    std::uint64_t count{};
    std::uint64_t failed_count{};
    std::uint64_t wall_time_ns{};
    std::uint64_t cpu_time_ns{};
    std::uint64_t peak_rss_bytes{};
    std::uint64_t maximum_rss_growth_bytes{};
};

struct Diagnostics {
    std::uint64_t invalid_phase_count{};
    std::uint64_t unmatched_end_count{};
    std::uint64_t mismatched_end_count{};
    std::uint64_t unfinished_region_count{};
};

struct Report {
    bool enabled{false};
    std::uint64_t sample_interval_ns{};
    std::array<Aggregate, kPhaseCount> phases{};
    std::vector<Record> records;
    Diagnostics diagnostics;
};

class Profiler {
public:
    explicit Profiler(
        std::chrono::nanoseconds sample_interval = kDefaultSampleInterval);
    ~Profiler();

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;
    Profiler(Profiler&&) = delete;
    Profiler& operator=(Profiler&&) = delete;

    void enable(bool reset = true);
    void disable() noexcept;
    bool enabled() const noexcept;

    Token start(Phase phase);
    bool end(Token token, Status status = Status::Success) noexcept;
    Report report() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

class Scope {
public:
    explicit Scope(Phase phase);
    Scope(Profiler& profiler, Phase phase);
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

private:
    Profiler* profiler_{};
    Token token_{kInvalidToken};
    int uncaught_exceptions_{};
};

Profiler& globalProfiler();
void enable(bool reset = true);
void disable() noexcept;
bool enabled() noexcept;
Token start(Phase phase);
bool end(Token token, Status status = Status::Success) noexcept;
Report report();

std::string_view phaseName(Phase phase) noexcept;
std::string_view statusName(Status status) noexcept;

}  // namespace cpygraph::profile
