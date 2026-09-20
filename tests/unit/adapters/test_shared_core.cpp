#include "api/adapters.h"
#include "test_support.h"

#include <limits>
#include <stdexcept>

#define CPYGRAPH_TEST_LOAD_GLOBAL 10
#define CPYGRAPH_TEST_CALL 11
#define CPYGRAPH_TEST_FOR_ITER 12
#define CPYGRAPH_TEST_IMPORT_NAME 13
#define CPYGRAPH_TEST_MAKE_FUNCTION 14
#define CPYGRAPH_TEST_EXTENDED_ARG 144

namespace {

cpygraph::bytecode::AdapterFeatures sharedCoreFeatures() {
    cpygraph::bytecode::AdapterFeatures features;
    features.call_protocol_inputs = 2;
    features.flagged_load_global = CPYGRAPH_TEST_LOAD_GLOBAL;
    features.conditional_stack_rules = {
        {CPYGRAPH_TEST_FOR_ITER, 0, 1, 1, 0, 1},
    };
    features.normalize_create_function = true;
    features.create_function_flag_inputs = true;
    return features;
}

class MinimalVersionAdapter final : public cpygraph::bytecode::TableDrivenCPythonAdapter {
public:
    MinimalVersionAdapter()
        : TableDrivenCPythonAdapter({
              {CPYGRAPH_TEST_LOAD_GLOBAL,
               {cpygraph::bytecode::SemanticOpcode::LoadGlobal,
                cpygraph::bytecode::OperandSource::Name}},
              {CPYGRAPH_TEST_CALL,
               {cpygraph::bytecode::SemanticOpcode::Call,
                cpygraph::bytecode::OperandSource::Count}},
              {CPYGRAPH_TEST_FOR_ITER,
               {cpygraph::bytecode::SemanticOpcode::ConditionalBranch,
                cpygraph::bytecode::OperandSource::None,
                cpygraph::bytecode::JumpKind::RelativeForward}},
              {CPYGRAPH_TEST_IMPORT_NAME,
               {cpygraph::bytecode::SemanticOpcode::ImportModule,
                cpygraph::bytecode::OperandSource::Name}},
              {CPYGRAPH_TEST_MAKE_FUNCTION,
               {cpygraph::bytecode::SemanticOpcode::CreateFunction,
                cpygraph::bytecode::OperandSource::Count}},
          },
          CPYGRAPH_TEST_EXTENDED_ARG, false, 0, false, sharedCoreFeatures()) {}

    std::string_view version() const noexcept override { return "test"; }
};

}  // namespace

int main() {
    using namespace cpygraph::bytecode;
    MinimalVersionAdapter adapter;
    CodeMetadata metadata;
    metadata.names = {"target"};

    const auto global = adapter.lift(RawInstruction{0, CPYGRAPH_TEST_LOAD_GLOBAL, 1}, metadata);
    require(global.symbol == "target" && global.push_call_protocol_marker,
            "shared adapter core decodes flagged name operands and call markers");

    const auto call = adapter.lift(RawInstruction{2, CPYGRAPH_TEST_CALL, 3}, metadata);
    require(call.call_protocol_input_count == 2,
            "shared adapter core applies the version call protocol");

    const auto loop = adapter.lift(RawInstruction{4, CPYGRAPH_TEST_FOR_ITER, 2}, metadata);
    require(loop.explicit_conditional_stack_effect && loop.stack_output_count == 1 &&
                loop.jump_stack_input_count == 1 && loop.stack_peek_count == 1,
            "shared adapter core applies declarative conditional stack behavior");

    const auto imported = adapter.lift(
        RawInstruction{6, CPYGRAPH_TEST_IMPORT_NAME, 0}, metadata);
    require(imported.discarded_stack_values == 2,
            "shared adapter core applies common import behavior");

    const auto function = adapter.lift(
        RawInstruction{8, CPYGRAPH_TEST_MAKE_FUNCTION, 3}, metadata);
    require(function.operand == 0 && function.auxiliary_input_count == 2,
            "shared adapter core derives function inputs from declarative features");

    bool rejected_wrapped_jump = false;
    try {
        adapter.lift(
            RawInstruction{4, CPYGRAPH_TEST_FOR_ITER,
                           std::numeric_limits<std::uint32_t>::max()},
            metadata);
    } catch (const std::overflow_error&) {
        rejected_wrapped_jump = true;
    }
    require(rejected_wrapped_jump,
            "shared adapter core rejects overflowing jump targets");
}
