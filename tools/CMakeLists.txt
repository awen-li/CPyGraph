function(add_cpygraph_tool name source component)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE CPyGraph::Shared)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)

    if(BUILD_TESTING)
        add_test(NAME tool_${name}_smoke COMMAND ${name}
            "${CMAKE_SOURCE_DIR}/tests/fixtures/sample_package")
        set_tests_properties(tool_${name}_smoke PROPERTIES
            LABELS "tools;${component}"
            PASS_REGULAR_EXPRESSION
                "\"component\":\"${component}\".*\"profiling\""
        )
        add_test(NAME tool_${name}_no_profile COMMAND ${name}
            "${CMAKE_SOURCE_DIR}/tests/fixtures/sample_package" --no-profile)
        set_tests_properties(tool_${name}_no_profile PROPERTIES
            LABELS "tools;${component}"
            PASS_REGULAR_EXPRESSION "\"component\":\"${component}\""
            FAIL_REGULAR_EXPRESSION "\"profiling\""
        )
    endif()
endfunction()

add_cpygraph_tool(pygCG pygCG/main.cpp cg)
add_cpygraph_tool(pygCFG pygCFG/main.cpp cfg)
add_cpygraph_tool(pygCDG pygCDG/main.cpp cdg)
add_cpygraph_tool(pygDDG pygDDG/main.cpp ddg)
