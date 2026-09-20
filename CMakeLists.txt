cmake_minimum_required(VERSION 3.16)
project(CPyGraph VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(Threads REQUIRED)

option(CPYGRAPH_BUILD_TOOLS "Build CPyGraph diagnostic tools" OFF)
set(CPYGRAPH_ABLATION "none" CACHE STRING
    "Controlled evaluation ablation: none, field-model, bound-method, exception-flow, or interprocedural-ddg")
set_property(CACHE CPYGRAPH_ABLATION PROPERTY STRINGS
    none field-model bound-method exception-flow interprocedural-ddg)
set(CPYGRAPH_VALID_ABLATIONS
    none field-model bound-method exception-flow interprocedural-ddg)
if(NOT CPYGRAPH_ABLATION IN_LIST CPYGRAPH_VALID_ABLATIONS)
    message(FATAL_ERROR "Unsupported CPYGRAPH_ABLATION: ${CPYGRAPH_ABLATION}")
endif()

file(GLOB CPYGRAPH_VERSION_ADAPTER_SOURCES CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "cpygraph/bytecode/adapters/python*/adapter.cpp"
)
list(SORT CPYGRAPH_VERSION_ADAPTER_SOURCES)
if(NOT CPYGRAPH_VERSION_ADAPTER_SOURCES)
    message(FATAL_ERROR "No CPython version adapters were discovered")
endif()

set(CPYGRAPH_ADAPTER_REGISTRY_INCLUDES "")
set(CPYGRAPH_ADAPTER_REGISTRY_ENTRIES "")
foreach(adapter_source IN LISTS CPYGRAPH_VERSION_ADAPTER_SOURCES)
    get_filename_component(adapter_directory "${adapter_source}" DIRECTORY)
    get_filename_component(version_directory "${adapter_directory}" NAME)
    if(NOT version_directory MATCHES "^python[0-9]+$")
        message(FATAL_ERROR
            "Invalid CPython adapter directory: ${version_directory}")
    endif()
    string(REGEX REPLACE "^python" "" compact_version "${version_directory}")
    string(LENGTH "${compact_version}" compact_version_length)
    if(compact_version_length LESS 2)
        message(FATAL_ERROR
            "CPython adapter directory must encode major and minor versions: ${version_directory}")
    endif()
    string(SUBSTRING "${compact_version}" 0 1 major_version)
    string(SUBSTRING "${compact_version}" 1 -1 minor_version)
    string(APPEND CPYGRAPH_ADAPTER_REGISTRY_INCLUDES
        "#include \"bytecode/adapters/${version_directory}/adapter.h\"\n")
    string(APPEND CPYGRAPH_ADAPTER_REGISTRY_ENTRIES
        "    if (version == \"${major_version}.${minor_version}\")\n"
        "        return std::make_unique<Python${compact_version}Adapter>();\n")
endforeach()

set(CPYGRAPH_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CPYGRAPH_GENERATED_DIRECTORY}")
file(GENERATE
    OUTPUT "${CPYGRAPH_GENERATED_DIRECTORY}/cpygraph_adapter_registry_includes.inc"
    CONTENT "${CPYGRAPH_ADAPTER_REGISTRY_INCLUDES}"
)
file(GENERATE
    OUTPUT "${CPYGRAPH_GENERATED_DIRECTORY}/cpygraph_adapter_registry_entries.inc"
    CONTENT "${CPYGRAPH_ADAPTER_REGISTRY_ENTRIES}"
)

set(CPYGRAPH_SOURCES
    cpygraph/bytecode/python_protocol.cpp
    cpygraph/bytecode/raw_bytecode.cpp
    cpygraph/bytecode/adapters/adapter.cpp
    cpygraph/bytecode/adapters/table_adapter.cpp
    ${CPYGRAPH_VERSION_ADAPTER_SOURCES}
    cpygraph/bytecode/adapters/adapter_factory.cpp
    cpygraph/analysis/profile/profiler.cpp
    cpygraph/analysis/cfg/control_flow_graph.cpp
    cpygraph/analysis/cfg/cfg_builder.cpp
    cpygraph/analysis/cfg/cfg_refiner.cpp
    cpygraph/analysis/cdg/post_dominators.cpp
    cpygraph/analysis/cdg/control_dependency_graph.cpp
    cpygraph/analysis/cdg/cdg_builder.cpp
    cpygraph/analysis/cg/call_graph.cpp
    cpygraph/analysis/cg/on_the_fly.cpp
    cpygraph/analysis/argument_binding.cpp
    cpygraph/analysis/python/language_features.cpp
    cpygraph/analysis/python/protocol.cpp
    cpygraph/analysis/ddg/data_dependency_graph.cpp
    cpygraph/analysis/ddg/ddg_builder.cpp
    cpygraph/analysis/ddg/interprocedural.cpp
    cpygraph/analysis/pta/andersen.cpp
    cpygraph/analysis/pta/import_alias_model.cpp
    cpygraph/analysis/pta/semantic_pta.cpp
    cpygraph/analysis/pta/interprocedural_pta.cpp
    cpygraph/bytecode/cpython_code_object_loader.cpp
    cpygraph/package/package.cpp
    cpygraph/package/analysis.cpp
    cpygraph/package/call_graph.cpp
    cpygraph/package/graph_builder.cpp
    cpygraph/visualization/dot_generator.cpp
)

add_library(cpygraph_objects OBJECT ${CPYGRAPH_SOURCES})
target_include_directories(cpygraph_objects
    PUBLIC cpygraph
    PRIVATE "${CPYGRAPH_GENERATED_DIRECTORY}"
)
target_link_libraries(cpygraph_objects PUBLIC Python3::Python Threads::Threads)
target_compile_definitions(cpygraph_objects PRIVATE
    CPYGRAPH_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
)
if(CPYGRAPH_ABLATION STREQUAL "field-model")
    target_compile_definitions(cpygraph_objects PRIVATE
        CPYGRAPH_ABLATE_FIELD_MODEL)
elseif(CPYGRAPH_ABLATION STREQUAL "bound-method")
    target_compile_definitions(cpygraph_objects PRIVATE
        CPYGRAPH_ABLATE_BOUND_METHOD)
elseif(CPYGRAPH_ABLATION STREQUAL "exception-flow")
    target_compile_definitions(cpygraph_objects PRIVATE
        CPYGRAPH_ABLATE_EXCEPTION_FLOW)
elseif(CPYGRAPH_ABLATION STREQUAL "interprocedural-ddg")
    target_compile_definitions(cpygraph_objects PRIVATE
        CPYGRAPH_ABLATE_INTERPROCEDURAL_DDG)
endif()
target_compile_options(cpygraph_objects PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(cpygraph_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(cpygraph_static STATIC $<TARGET_OBJECTS:cpygraph_objects>)
target_include_directories(cpygraph_static PUBLIC cpygraph)
target_link_libraries(cpygraph_static PUBLIC Python3::Python Threads::Threads)
set_target_properties(cpygraph_static PROPERTIES OUTPUT_NAME cpygraph)

add_library(cpygraph_shared SHARED $<TARGET_OBJECTS:cpygraph_objects>)
target_include_directories(cpygraph_shared PUBLIC cpygraph)
target_link_libraries(cpygraph_shared PUBLIC Python3::Python Threads::Threads)
set_target_properties(cpygraph_shared PROPERTIES
    OUTPUT_NAME cpygraph
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_library(CPyGraph::Static ALIAS cpygraph_static)
add_library(CPyGraph::Shared ALIAS cpygraph_shared)

include(CTest)
option(CPYGRAPH_BUILD_BENCHMARKS "Build the PYGBench suite" ${BUILD_TESTING})

if(CPYGRAPH_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(BUILD_TESTING)
    function(add_cpygraph_unit_test name component source)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE tests/unit)
        target_link_libraries(${name} PRIVATE CPyGraph::Static)
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES LABELS "unit;${component}")
    endfunction()

    add_cpygraph_unit_test(unit_adapters adapters tests/unit/adapters/test_adapters.cpp)
    add_cpygraph_unit_test(unit_adapters_shared_core adapters
        tests/unit/adapters/test_shared_core.cpp)
    add_cpygraph_unit_test(unit_cfg cfg tests/unit/cfg/test_cfg.cpp)
    add_cpygraph_unit_test(unit_cdg cdg tests/unit/cdg/test_cdg.cpp)
    add_cpygraph_unit_test(unit_pta_andersen pta tests/unit/pta/test_andersen.cpp)
    add_cpygraph_unit_test(unit_pta_import_aliases pta tests/unit/pta/test_import_aliases.cpp)
    add_cpygraph_unit_test(unit_pta_python_model pta tests/unit/pta/test_python_model.cpp)
    add_cpygraph_unit_test(unit_pta_python_language_features pta
        tests/unit/pta/test_python_language_features.cpp)
    add_cpygraph_unit_test(unit_pta_semantic_constraints pta
        tests/unit/pta/test_semantic_constraints.cpp)
    add_cpygraph_unit_test(unit_pta_sensitivity pta
        tests/unit/pta/test_sensitivity.cpp)
    add_cpygraph_unit_test(unit_cg cg tests/unit/cg/test_call_graph.cpp)
    add_cpygraph_unit_test(unit_cg_on_the_fly cg
        tests/unit/cg/test_on_the_fly.cpp)
    add_cpygraph_unit_test(unit_ddg ddg tests/unit/ddg/test_ddg.cpp)

    add_cpygraph_unit_test(unit_package package tests/unit/package/test_package.cpp)
    target_compile_definitions(unit_package PRIVATE
        CPYGRAPH_TEST_PACKAGE_PATH="${CMAKE_SOURCE_DIR}/tests/fixtures/sample_package"
    )

    add_cpygraph_unit_test(unit_visualization visualization
        tests/unit/visualization/test_dot.cpp)

    add_cpygraph_unit_test(unit_python_protocols python
        tests/unit/python/test_protocols.cpp)
    add_cpygraph_unit_test(unit_python_package_features python
        tests/unit/python/test_package_features.cpp)
    target_compile_definitions(unit_python_package_features PRIVATE
        CPYGRAPH_TEST_PYTHON_FEATURE_PACKAGE="${CMAKE_SOURCE_DIR}/tests/fixtures/python_features"
    )

    add_cpygraph_unit_test(unit_profile profile
        tests/unit/profile/test_profiler.cpp)

    add_test(
        NAME experiment_resource_monitor
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/experiments/test_resource_monitor.py"
    )
    set_tests_properties(experiment_resource_monitor PROPERTIES
        LABELS "unit;experiments;profile")

    add_test(
        NAME experiment_campaign_plan
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/experiments/test_campaign.py"
    )
    set_tests_properties(experiment_campaign_plan PROPERTIES
        LABELS "unit;experiments")

    add_test(
        NAME experiment_cross_version
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/experiments/test_cross_version.py"
    )
    set_tests_properties(experiment_cross_version PROPERTIES
        LABELS "unit;experiments")

    add_test(
        NAME experiment_ablations
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/experiments/test_ablations.py"
    )
    set_tests_properties(experiment_ablations PROPERTIES
        LABELS "unit;experiments")

    add_test(
        NAME experiment_bundled_bytecode
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/experiments/test_bundled_bytecode.py"
    )
    set_tests_properties(experiment_bundled_bytecode PROPERTIES
        LABELS "unit;experiments;bytecode")

    add_test(
        NAME experiment_pycg_micro_scoring
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/experiments/test_pycg_micro.py"
    )
    set_tests_properties(experiment_pycg_micro_scoring PROPERTIES
        LABELS "unit;experiments;cg")

    if(CPYGRAPH_BUILD_TOOLS)
        add_test(
            NAME experiment_full_pipeline_tool
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/experiments/test_full_pipeline_tool.py"
                    --tool "$<TARGET_FILE:pygDDG>"
                    --input "${CMAKE_SOURCE_DIR}/tests/fixtures/sample_package"
        )
        set_tests_properties(experiment_full_pipeline_tool PROPERTIES
            LABELS "unit;experiments;tools")
    endif()

endif()

if(CPYGRAPH_BUILD_BENCHMARKS)
    add_executable(pygbench_case_analysis tests/pygbench/case_analysis.cpp)
    target_link_libraries(pygbench_case_analysis PRIVATE CPyGraph::Static)

    if(BUILD_TESTING)
        add_test(
            NAME pygbench_cases
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/PYGBench/run.py" cases
                    --executable "$<TARGET_FILE:pygbench_case_analysis>"
                    --quiet
        )
        set_tests_properties(pygbench_cases PROPERTIES
            LABELS "microbench;case;pygbench")
        add_test(
            NAME pygbench_ground_truth
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/PYGBench/run.py" ground-truth
        )
        set_tests_properties(pygbench_ground_truth PROPERTIES
            LABELS "microbench;oracle;pygbench")
        add_test(
            NAME pygbench_result_scorer
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/PYGBench/groundtruths/score.py"
                    --self-test
        )
        set_tests_properties(pygbench_result_scorer PROPERTIES
            LABELS "microbench;results;pygbench")
        add_test(
            NAME pygbench_canonical_comparator
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/PYGBench/groundtruths/compare.py"
                    --self-test
        )
        set_tests_properties(pygbench_canonical_comparator PROPERTIES
            LABELS "microbench;results;pygbench")
        add_test(
            NAME pygbench_complete_results
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/PYGBench/run.py" evaluate
                    --executable "$<TARGET_FILE:pygbench_case_analysis>"
        )
        set_tests_properties(pygbench_complete_results PROPERTIES
            LABELS "microbench;results;pygbench")
        add_test(
            NAME pygbench_corpus
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/PYGBench/run.py" validate
        )
        set_tests_properties(pygbench_corpus PROPERTIES
            LABELS "microbench;pygbench")
    endif()
endif()
