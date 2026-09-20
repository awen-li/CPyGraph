#include "analysis/profile/profiler.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <ctime>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace cpygraph::profile {
namespace {

constexpr std::uint64_t kNanosecondsPerSecond = 1000000000ULL;

std::uint64_t currentRssBytes() noexcept {
#if defined(__linux__)
    std::ifstream status("/proc/self/statm");
    std::uint64_t virtual_pages = 0U;
    std::uint64_t resident_pages = 0U;
    if (!(status >> virtual_pages >> resident_pages)) return 0U;
    const auto page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return 0U;
    const auto unsigned_page_size = static_cast<std::uint64_t>(page_size);
    if (resident_pages >
        std::numeric_limits<std::uint64_t>::max() / unsigned_page_size)
        return 0U;
    return resident_pages * unsigned_page_size;
#else
    return 0U;
#endif
}

std::uint64_t wallTimeNanoseconds() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint64_t cpuTimeNanoseconds() noexcept {
    const auto ticks = std::clock();
    if (ticks == static_cast<std::clock_t>(-1)) return 0U;
    const auto seconds = static_cast<long double>(ticks) /
                         static_cast<long double>(CLOCKS_PER_SEC);
    const auto nanoseconds = seconds *
                             static_cast<long double>(kNanosecondsPerSecond);
    if (nanoseconds <= 0.0L) return 0U;
    if (nanoseconds >= static_cast<long double>(
                           std::numeric_limits<std::uint64_t>::max()))
        return std::numeric_limits<std::uint64_t>::max();
    return static_cast<std::uint64_t>(nanoseconds);
}

std::uint64_t elapsed(std::uint64_t end, std::uint64_t start) noexcept {
    return end >= start ? end - start : 0U;
}

std::int64_t rssDelta(std::uint64_t end, std::uint64_t start) noexcept {
    constexpr auto maximum =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (end >= start)
        return static_cast<std::int64_t>(std::min(end - start, maximum));
    return -static_cast<std::int64_t>(std::min(start - end, maximum));
}

bool validPhase(Phase phase) noexcept {
    return static_cast<std::size_t>(phase) < kPhaseCount;
}

}  // namespace

struct Profiler::Implementation {
    struct ActiveRegion {
        Token token{kInvalidToken};
        Phase phase{Phase::EndToEnd};
        std::thread::id thread;
        std::uint64_t wall_start_ns{};
        std::uint64_t cpu_start_ns{};
        std::uint64_t rss_start_bytes{};
        std::uint64_t peak_rss_bytes{};
    };

    explicit Implementation(std::chrono::nanoseconds interval)
        : sample_interval(interval) {
        if (sample_interval <= std::chrono::nanoseconds::zero())
            throw std::invalid_argument(
                "profiling sample interval must be positive");
    }

    void sampleMemory() noexcept {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stop_sampler) {
            if (sampler_condition.wait_for(
                    lock, sample_interval,
                    [this] { return stop_sampler; }))
                break;
            lock.unlock();
            const auto rss = currentRssBytes();
            lock.lock();
            for (auto& item : active)
                item.second.peak_rss_bytes =
                    std::max(item.second.peak_rss_bytes, rss);
        }
    }

    const std::chrono::nanoseconds sample_interval;
    std::atomic<bool> enabled{true};
    std::mutex lifecycle_mutex;
    mutable std::mutex mutex;
    std::condition_variable sampler_condition;
    bool stop_sampler{false};
    Token next_token{1U};
    std::unordered_map<Token, ActiveRegion> active;
    std::unordered_map<std::thread::id, std::vector<Token>> thread_stacks;
    std::vector<Record> records;
    Diagnostics diagnostics;
    std::thread sampler;
};

Profiler::Profiler(std::chrono::nanoseconds sample_interval)
    : implementation_(std::make_unique<Implementation>(sample_interval)) {}

Profiler::~Profiler() { disable(); }

void Profiler::enable(bool reset) {
    auto& state = *implementation_;
    std::lock_guard<std::mutex> lifecycle_lock(state.lifecycle_mutex);
    std::lock_guard<std::mutex> lock(state.mutex);
    if (reset) {
        if (!state.active.empty())
            throw std::logic_error(
                "cannot reset profiling while regions are active");
        state.records.clear();
        state.diagnostics = {};
    }
    state.enabled.store(true, std::memory_order_release);
}

void Profiler::disable() noexcept {
    auto& state = *implementation_;
    std::lock_guard<std::mutex> lifecycle_lock(state.lifecycle_mutex);
    std::thread sampler;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.enabled.store(false, std::memory_order_release);
        state.diagnostics.unfinished_region_count += state.active.size();
        state.active.clear();
        state.thread_stacks.clear();
        state.stop_sampler = true;
        sampler = std::move(state.sampler);
    }
    state.sampler_condition.notify_all();
    if (!sampler.joinable()) return;
    sampler.join();
}

bool Profiler::enabled() const noexcept {
    return implementation_->enabled.load(std::memory_order_acquire);
}

Token Profiler::start(Phase phase) {
    auto& state = *implementation_;
    if (!state.enabled.load(std::memory_order_acquire)) return kInvalidToken;
    const auto rss = currentRssBytes();
    const auto wall = wallTimeNanoseconds();
    const auto cpu = cpuTimeNanoseconds();
    const auto thread = std::this_thread::get_id();
    std::lock_guard<std::mutex> lifecycle_lock(state.lifecycle_mutex);
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.enabled.load(std::memory_order_relaxed)) return kInvalidToken;
    if (!validPhase(phase)) {
        ++state.diagnostics.invalid_phase_count;
        return kInvalidToken;
    }
    if (!state.sampler.joinable()) {
        state.stop_sampler = false;
        state.sampler = std::thread([&state] { state.sampleMemory(); });
    }
    auto token = state.next_token++;
    if (token == kInvalidToken) token = state.next_token++;
    state.active.emplace(
        token, Implementation::ActiveRegion{
                   token, phase, thread, wall, cpu, rss, rss});
    state.thread_stacks[thread].push_back(token);
    return token;
}

bool Profiler::end(Token token, Status status) noexcept {
    if (token == kInvalidToken) return false;
    auto& state = *implementation_;
    const auto rss = currentRssBytes();
    const auto wall = wallTimeNanoseconds();
    const auto cpu = cpuTimeNanoseconds();
    const auto thread = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto found = state.active.find(token);
    if (found == state.active.end() || found->second.thread != thread) {
        ++state.diagnostics.unmatched_end_count;
        return false;
    }
    const auto stack = state.thread_stacks.find(thread);
    if (stack == state.thread_stacks.end() || stack->second.empty() ||
        stack->second.back() != token) {
        ++state.diagnostics.mismatched_end_count;
        return false;
    }
    const auto region = found->second;
    stack->second.pop_back();
    if (stack->second.empty()) state.thread_stacks.erase(stack);
    state.active.erase(found);
    const auto peak = std::max(region.peak_rss_bytes, rss);
    state.records.push_back({
        region.phase,
        status,
        elapsed(wall, region.wall_start_ns),
        elapsed(cpu, region.cpu_start_ns),
        region.rss_start_bytes,
        rss,
        peak,
        rssDelta(rss, region.rss_start_bytes),
    });
    return true;
}

Report Profiler::report() const {
    const auto& state = *implementation_;
    std::lock_guard<std::mutex> lock(state.mutex);
    Report result;
    result.enabled = state.enabled.load(std::memory_order_relaxed);
    result.sample_interval_ns =
        static_cast<std::uint64_t>(state.sample_interval.count());
    result.records = state.records;
    result.diagnostics = state.diagnostics;
    for (std::size_t index = 0U; index < result.phases.size(); ++index)
        result.phases[index].phase = static_cast<Phase>(index);
    for (const auto& record : result.records) {
        auto& aggregate =
            result.phases[static_cast<std::size_t>(record.phase)];
        ++aggregate.count;
        aggregate.failed_count += record.status == Status::Failed ? 1U : 0U;
        aggregate.wall_time_ns += record.wall_time_ns;
        aggregate.cpu_time_ns += record.cpu_time_ns;
        aggregate.peak_rss_bytes =
            std::max(aggregate.peak_rss_bytes, record.peak_rss_bytes);
        if (record.rss_delta_bytes > 0)
            aggregate.maximum_rss_growth_bytes = std::max(
                aggregate.maximum_rss_growth_bytes,
                static_cast<std::uint64_t>(record.rss_delta_bytes));
    }
    return result;
}

Scope::Scope(Phase phase) : Scope(globalProfiler(), phase) {}

Scope::Scope(Profiler& profiler, Phase phase)
    : profiler_(&profiler), token_(profiler.start(phase)),
      uncaught_exceptions_(std::uncaught_exceptions()) {}

Scope::~Scope() {
    if (token_ == kInvalidToken) return;
    const auto status = std::uncaught_exceptions() > uncaught_exceptions_
                            ? Status::Failed
                            : Status::Success;
    profiler_->end(token_, status);
}

Profiler& globalProfiler() {
    static Profiler profiler;
    return profiler;
}

void enable(bool reset) { globalProfiler().enable(reset); }

void disable() noexcept { globalProfiler().disable(); }

bool enabled() noexcept { return globalProfiler().enabled(); }

Token start(Phase phase) { return globalProfiler().start(phase); }

bool end(Token token, Status status) noexcept {
    return globalProfiler().end(token, status);
}

Report report() { return globalProfiler().report(); }

std::string_view phaseName(Phase phase) noexcept {
    switch (phase) {
        case Phase::EndToEnd: return "end_to_end";
        case Phase::PackageCompilation: return "package_compilation";
        case Phase::PackageLoading: return "package_loading";
        case Phase::BytecodeAnalysis: return "bytecode_analysis";
        case Phase::PointsToCallGraph: return "points_to_call_graph";
        case Phase::ControlFlowGraph: return "control_flow_graph";
        case Phase::ControlDependencyGraph: return "control_dependency_graph";
        case Phase::DataDependencyGraph: return "data_dependency_graph";
        case Phase::Count: return "invalid";
    }
    return "invalid";
}

std::string_view statusName(Status status) noexcept {
    switch (status) {
        case Status::Success: return "success";
        case Status::Failed: return "failed";
    }
    return "invalid";
}

}  // namespace cpygraph::profile
