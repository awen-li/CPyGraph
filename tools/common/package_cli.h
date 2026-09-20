#pragma once

#include "api/package.h"
#include "api/profile.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cpygraph::tools {

constexpr int kSuccessExitCode = 0;
constexpr int kUsageExitCode = 2;
constexpr int kAnalysisFailureExitCode = 1;
constexpr int kProgramArgumentCount = 2;
constexpr int kProgramArgumentCountWithOption = 3;
constexpr std::string_view kDisableProfilingArgument = "--no-profile";
constexpr char kBytecodeExtension[] = ".pyc";

struct PackageToolArguments {
    std::filesystem::path package_path;
    bool profiling_enabled{true};
};

inline bool parsePackageToolArguments(
    int argc, char** argv, PackageToolArguments& result) {
    if (argc != kProgramArgumentCount &&
        argc != kProgramArgumentCountWithOption)
        return false;
    bool found_package = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == kDisableProfilingArgument) {
            if (!result.profiling_enabled) return false;
            result.profiling_enabled = false;
            continue;
        }
        if (found_package || argument.rfind("--", 0U) == 0U) return false;
        result.package_path = argv[index];
        found_package = true;
    }
    return found_package;
}

inline package::PackageAnalysis loadPackageAnalysis(
    const std::filesystem::path& package_path) {
    if (package_path.extension() == kBytecodeExtension) {
        auto loaded = package::PackageLoader().loadBytecode(package_path);
        return package::PackageAnalyzer().analyze(loaded);
    }
    package::PackageCompiler compiler;
    auto compiled = compiler.compile(package_path);
    auto loaded = package::PackageLoader().load(compiled);
    return package::PackageAnalyzer().analyze(loaded);
}

inline void writeProfilingReport(std::ostream& output,
                                 const profile::Report& report) {
    output << ",\"profiling\":{\"sample_interval_ns\":"
           << report.sample_interval_ns << ",\"phases\":[";
    bool first = true;
    for (const auto& phase : report.phases) {
        if (phase.count == 0U) continue;
        if (!first) output << ',';
        first = false;
        output << "{\"phase\":\"" << profile::phaseName(phase.phase)
               << "\",\"count\":" << phase.count
               << ",\"failed_count\":" << phase.failed_count
               << ",\"wall_time_ns\":" << phase.wall_time_ns
               << ",\"cpu_time_ns\":" << phase.cpu_time_ns
               << ",\"peak_rss_bytes\":" << phase.peak_rss_bytes
               << ",\"maximum_rss_growth_bytes\":"
               << phase.maximum_rss_growth_bytes << '}';
    }
    output << "],\"records\":[";
    first = true;
    for (const auto& record : report.records) {
        if (!first) output << ',';
        first = false;
        output << "{\"phase\":\"" << profile::phaseName(record.phase)
               << "\",\"status\":\"" << profile::statusName(record.status)
               << "\",\"wall_time_ns\":" << record.wall_time_ns
               << ",\"cpu_time_ns\":" << record.cpu_time_ns
               << ",\"rss_start_bytes\":" << record.rss_start_bytes
               << ",\"rss_end_bytes\":" << record.rss_end_bytes
               << ",\"peak_rss_bytes\":" << record.peak_rss_bytes
               << ",\"rss_delta_bytes\":" << record.rss_delta_bytes << '}';
    }
    output << "],\"diagnostics\":{\"invalid_phase_count\":"
           << report.diagnostics.invalid_phase_count
           << ",\"unmatched_end_count\":"
           << report.diagnostics.unmatched_end_count
           << ",\"mismatched_end_count\":"
           << report.diagnostics.mismatched_end_count
           << ",\"unfinished_region_count\":"
           << report.diagnostics.unfinished_region_count << "}}";
}

template <typename Analysis>
int runPackageTool(int argc, char** argv, std::string_view tool_name,
                   Analysis&& analysis) {
    PackageToolArguments arguments;
    if (!parsePackageToolArguments(argc, argv, arguments)) {
        std::cerr << "usage: " << tool_name
                  << " <package-path> [--no-profile]\n";
        return kUsageExitCode;
    }
    if (arguments.profiling_enabled)
        profile::enable(true);
    else
        profile::disable();
    const auto end_to_end = profile::start(profile::Phase::EndToEnd);
    try {
        const auto package = loadPackageAnalysis(arguments.package_path);
        std::ostringstream fields;
        analysis(package, fields);
        profile::end(end_to_end);
        const auto profiling = profile::report();
        std::cout << '{' << fields.str();
        if (arguments.profiling_enabled)
            writeProfilingReport(std::cout, profiling);
        std::cout << "}\n";
        profile::disable();
        return kSuccessExitCode;
    } catch (const std::exception& error) {
        profile::end(end_to_end, profile::Status::Failed);
        profile::disable();
        std::cerr << tool_name << ": " << error.what() << '\n';
        return kAnalysisFailureExitCode;
    }
}

}  // namespace cpygraph::tools
