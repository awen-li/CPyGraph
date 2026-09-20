#include "api/cfg.h"
#include "api/package.h"
#include "api/pta.h"
#include "test_support.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr cpygraph::CodeObjectId kModuleCode = 1U;
constexpr std::size_t kFirstAllocationIndex = 0U;
constexpr std::size_t kFirstLoadIndex = 2U;
constexpr std::size_t kSecondAllocationIndex = 4U;

cpygraph::package::PackageAnalysis makeFlowPackage() {
    using cpygraph::bytecode::SemanticOpcode;
    cpygraph::bytecode::SemanticProgram program{
        {0U, SemanticOpcode::LoadConst, 0U},
        {2U, SemanticOpcode::StoreLocal, 0U, "value"},
        {4U, SemanticOpcode::LoadLocal, 0U, "value"},
        {6U, SemanticOpcode::Pop},
        {8U, SemanticOpcode::LoadConst, 1U},
        {10U, SemanticOpcode::StoreLocal, 0U, "value"},
        {12U, SemanticOpcode::LoadLocal, 0U, "value"},
        {14U, SemanticOpcode::Return},
    };
    std::vector<cpygraph::package::CodeObjectAnalysis> code_objects;
    code_objects.push_back({
        kModuleCode,
        0U,
        "sensitivity_module",
        "<module>",
        "<module>",
        std::filesystem::path("sensitivity_module.py"),
        false,
        0U,
        0U,
        0U,
        false,
        false,
        {"value"},
        {0U, 0U},
        cpygraph::cfg::CFGBuilder().build(std::move(program)),
    });
    return {{}, "3.10", 1U, std::move(code_objects)};
}

cpygraph::ObjectId origin(
    const cpygraph::package::PackageAnalysisObservations& observations,
    std::size_t instruction_index) {
    const auto found = std::find_if(
        observations.object_origins.begin(), observations.object_origins.end(),
        [&](const auto& candidate) {
            return candidate.code == kModuleCode &&
                   candidate.kind ==
                       cpygraph::package::PTAObjectOriginKind::Instruction &&
                   candidate.index == instruction_index;
        });
    require(found != observations.object_origins.end(),
            "PTA sensitivity test allocation origin exists");
    return found->object;
}

const std::vector<cpygraph::ObjectId>& pointsTo(
    const cpygraph::package::PackageAnalysisObservations& observations,
    std::size_t instruction_index) {
    const auto found = std::find_if(
        observations.points_to.begin(), observations.points_to.end(),
        [&](const auto& candidate) {
            return candidate.code == kModuleCode &&
                   candidate.instruction_index == instruction_index;
        });
    require(found != observations.points_to.end(),
            "PTA sensitivity test value observation exists");
    return found->objects;
}

}  // namespace

int main() {
    const auto package = makeFlowPackage();
    cpygraph::package::PackageAnalysisObservations insensitive;
    cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
        package, {}, &insensitive);
    const auto first_default = origin(insensitive, kFirstAllocationIndex);
    const auto second_default = origin(insensitive, kSecondAllocationIndex);
    const auto& insensitive_load = pointsTo(insensitive, kFirstLoadIndex);
    require(std::find(insensitive_load.begin(), insensitive_load.end(),
                      first_default) != insensitive_load.end() &&
                std::find(insensitive_load.begin(), insensitive_load.end(),
                          second_default) != insensitive_load.end(),
            "default function policy is flow insensitive");

    cpygraph::PTASensitivityConfiguration selective;
    selective.level = cpygraph::PTASensitivityLevel::Selective;
    selective.functions = {{kModuleCode, cpygraph::PTASensitivity::Flow}};
    cpygraph::package::PackageAnalysisObservations selected;
    cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
        package, {}, &selected, selective);
    const auto first_selected = origin(selected, kFirstAllocationIndex);
    const auto second_selected = origin(selected, kSecondAllocationIndex);
    const auto& selected_load = pointsTo(selected, kFirstLoadIndex);
    require(std::find(selected_load.begin(), selected_load.end(),
                      first_selected) != selected_load.end() &&
                std::find(selected_load.begin(), selected_load.end(),
                          second_selected) == selected_load.end(),
            "selective flow sensitivity applies only at the configured function");

    cpygraph::PTASensitivityConfiguration complete;
    complete.level = cpygraph::PTASensitivityLevel::Complete;
    cpygraph::package::PackageAnalysisObservations all_sensitive;
    cpygraph::package::OnTheFlyCallGraphAnalyzer().analyze(
        package, {}, &all_sensitive, complete);
    require(std::all_of(all_sensitive.points_to.begin(),
                        all_sensitive.points_to.end(),
                        [](const auto& observation) {
                            return observation.context != 0U;
                        }),
            "complete sensitivity applies a PTA path context to every function");
}
