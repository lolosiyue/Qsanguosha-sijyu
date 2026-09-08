include_guard(GLOBAL)

# Also register the opt-in production-session consumer when fixtures are OFF.
include("${CMAKE_CURRENT_LIST_DIR}/QSanguoshaRulesSession.cmake")

# Available with TUI/GUI/server products disabled. Default-on only for tests;
# ordinary product packaging need not ship this development executable.
option(QSAN_BUILD_RULES_FIXTURE_RUNNER "Build the native client-rules JSON fixture runner" ${BUILD_TESTING})
option(QSAN_BUILD_WASM_RULES_FIXTURES "Build the experimental Emscripten rules fixture module" OFF)
if(NOT QSAN_BUILD_RULES_FIXTURE_RUNNER AND NOT QSAN_BUILD_WASM_RULES_FIXTURES)
    return()
endif()
if(QSAN_BUILD_XP_LEGACY)
    message(FATAL_ERROR "The rules fixture runner currently requires the normal Qt 6 toolchain")
endif()
if(EMSCRIPTEN AND NOT QSAN_BUILD_WASM_RULES_FIXTURES)
    message(FATAL_ERROR "Use the explicit QSAN_BUILD_WASM_RULES_FIXTURES cross-build option")
endif()

set(qsan_fixture_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests/client_runtime")
add_library(qsanguosha_rules_fixture_support STATIC
    "${qsan_fixture_dir}/selection-fixture.cpp"
    "${qsan_fixture_dir}/selection-fixture.h"
)
target_include_directories(qsanguosha_rules_fixture_support PUBLIC "${qsan_fixture_dir}")
target_link_libraries(qsanguosha_rules_fixture_support PUBLIC qsanguosha_client_runtime)
set_target_properties(qsanguosha_rules_fixture_support PROPERTIES FOLDER "Tests")

if(QSAN_BUILD_WASM_RULES_FIXTURES)
    include("${CMAKE_CURRENT_LIST_DIR}/QSanguoshaRulesWasm.cmake")
    return()
endif()

add_executable(qsanguosha_rules_fixture_runner
    "${qsan_fixture_dir}/selection-fixture-main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/interaction-reply-encoder.cpp"
)
target_link_libraries(qsanguosha_rules_fixture_runner PRIVATE
    qsanguosha_rules_fixture_support
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
)
set_target_properties(qsanguosha_rules_fixture_runner PROPERTIES FOLDER "Tests")
# Do not link tui_support, protocol test support, Widgets or a second normal
# engine link. Package registrars and the existing wire encoder are preserved.
if(WIN32)
    target_link_libraries(qsanguosha_rules_fixture_runner PRIVATE
        "$<$<CONFIG:Release>:dbghelp>"
        "$<$<CONFIG:Release>:user32>"
        "$<$<CONFIG:Release>:gdi32>"
    )
endif()
if(MSVC)
    foreach(target qsanguosha_rules_fixture_support qsanguosha_rules_fixture_runner)
        target_compile_options(${target} PRIVATE /utf-8 /bigobj)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endforeach()
endif()

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME qsanguosha_client_rules_fixtures
        COMMAND "${Python3_EXECUTABLE}" "${qsan_fixture_dir}/check-fixtures.py"
            --runner "$<TARGET_FILE:qsanguosha_rules_fixture_runner>"
            --fixtures "${qsan_fixture_dir}/fixtures"
            --asset-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --builtin-assets
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/client-rules-fixtures"
    )
    set_tests_properties(qsanguosha_client_rules_fixtures PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        LABELS "client;client-runtime;protocol;fast"
        TIMEOUT 240
        RUN_SERIAL TRUE
    )
    if(WIN32)
        set_property(TEST qsanguosha_client_rules_fixtures PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
    add_test(NAME qsanguosha_client_rules_fixture_harness
        COMMAND "${Python3_EXECUTABLE}" "${qsan_fixture_dir}/check-fixtures.py" --self-test)
    set_tests_properties(qsanguosha_client_rules_fixture_harness PROPERTIES
        LABELS "client;client-runtime;fast" TIMEOUT 15)
endif()
