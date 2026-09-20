#include "api/profile.h"
#include "test_support.h"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace {

constexpr auto kTestSampleInterval = std::chrono::milliseconds(1);
constexpr auto kMeasuredDelay = std::chrono::milliseconds(3);

const cpygraph::profile::Aggregate& aggregate(
    const cpygraph::profile::Report& report,
    cpygraph::profile::Phase phase) {
    return report.phases[static_cast<std::size_t>(phase)];
}

}  // namespace

int main() {
    using namespace cpygraph::profile;

    Profiler profiler(kTestSampleInterval);
    require(profiler.enabled(), "profiling is enabled by default");

    {
        Scope outer(profiler, Phase::EndToEnd);
        {
            Scope inner(profiler, Phase::PointsToCallGraph);
            std::this_thread::sleep_for(kMeasuredDelay);
        }
    }
    auto measured = profiler.report();
    require(measured.records.size() == 2U,
            "nested profiling regions retain individual records");
    require(aggregate(measured, Phase::EndToEnd).count == 1U &&
                aggregate(measured, Phase::PointsToCallGraph).count == 1U,
            "profiling report aggregates records by numeric phase");
    require(aggregate(measured, Phase::EndToEnd).wall_time_ns > 0U &&
                aggregate(measured, Phase::PointsToCallGraph).wall_time_ns > 0U,
            "profiling records elapsed wall time");
    require(measured.records[0].peak_rss_bytes >=
                measured.records[0].rss_start_bytes &&
                measured.records[0].peak_rss_bytes >=
                    measured.records[0].rss_end_bytes,
            "profiling peak RSS covers both boundary samples");

    try {
        Scope failed(profiler, Phase::BytecodeAnalysis);
        throw std::runtime_error("expected profiling test failure");
    } catch (const std::runtime_error&) {
    }
    measured = profiler.report();
    require(aggregate(measured, Phase::BytecodeAnalysis).failed_count == 1U,
            "RAII profiling records exceptional region completion");

    const auto outer = profiler.start(Phase::ControlFlowGraph);
    const auto inner = profiler.start(Phase::DataDependencyGraph);
    require(!profiler.end(outer),
            "profiling rejects a non-LIFO nested region end");
    require(profiler.end(inner) && profiler.end(outer),
            "profiling regions remain recoverable after a mismatched end");
    measured = profiler.report();
    require(measured.diagnostics.mismatched_end_count == 1U,
            "profiling reports mismatched nested regions numerically");

    profiler.disable();
    require(!profiler.enabled(), "profiling can be explicitly disabled");
    {
        Scope disabled(profiler, Phase::PackageLoading);
    }
    require(profiler.report().records.size() == measured.records.size(),
            "disabled profiling retains no additional records");

    profiler.enable();
    require(profiler.report().records.empty(),
            "re-enabling with reset clears prior measurements");
    const auto unfinished = profiler.start(Phase::PackageCompilation);
    require(unfinished != kInvalidToken,
            "enabled profiling returns a valid numeric token");
    profiler.disable();
    require(profiler.report().diagnostics.unfinished_region_count == 1U,
            "disabling reports unfinished profiling regions");
}
