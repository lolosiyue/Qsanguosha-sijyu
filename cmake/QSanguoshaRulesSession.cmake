include_guard(GLOBAL)

set(qsan_session_tests_default OFF)
if(BUILD_TESTING AND NOT EMSCRIPTEN AND NOT QSAN_BUILD_XP_LEGACY)
    set(qsan_session_tests_default ON)
endif()
option(QSAN_BUILD_RULES_SESSION_TESTS "Build the production rules session lifecycle probe"
    ${qsan_session_tests_default})
if(NOT QSAN_BUILD_RULES_SESSION_TESTS AND NOT QSAN_BUILD_WASM_WEB_CLIENT
        AND NOT QSAN_BUILD_EXCEL)
    return()
endif()
if(EMSCRIPTEN AND QSAN_BUILD_RULES_SESSION_TESTS)
    message(FATAL_ERROR "The native session probe requires Qt 6 without Emscripten/XP")
endif()

# One production implementation. Tests do not compile a copy of its behavior,
# and final executables retain responsibility for WHOLE_ARCHIVE engine linkage.
add_library(qsanguosha_rules_session STATIC
    src/client/runtime/client-rules-session.cpp
    src/client/runtime/client-rules-session.h
    src/client/interaction-reply-encoder.cpp
    src/client/protocol-interaction-request-builder.cpp
    src/client/interaction-request-factory.cpp
    src/client/interaction-command-registry.cpp
)
if(NOT QSAN_BUILD_XP_LEGACY)
    target_sources(qsanguosha_rules_session PRIVATE
        src/client/runtime/client-rules-host.cpp
        src/client/runtime/client-rules-host.h
        src/client/runtime/client-rules-ingress.cpp
        src/client/runtime/client-rules-ingress.h
    )
endif()
target_link_libraries(qsanguosha_rules_session PUBLIC qsanguosha_client_runtime)
set_target_properties(qsanguosha_rules_session PROPERTIES FOLDER "Libraries")
if(QSAN_BUILD_XP_LEGACY)
    target_include_directories(qsanguosha_rules_session BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5)
    target_compile_definitions(qsanguosha_rules_session PRIVATE
        QSAN_XP_LEGACY WIN32 WINVER=0x0501 _WIN32_WINNT=0x0501)
    if(MSVC)
        target_compile_options(qsanguosha_rules_session PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5/qsan-qt5-compat.h")
    endif()
endif()
if(EMSCRIPTEN)
    target_compile_options(qsanguosha_rules_session PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fexceptions>")
endif()
if(MSVC)
    target_compile_options(qsanguosha_rules_session PRIVATE /utf-8 /bigobj)
    target_compile_definitions(qsanguosha_rules_session PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(NOT QSAN_BUILD_RULES_SESSION_TESTS)
    return()
endif()
if(QSAN_BUILD_XP_LEGACY)
    # XP consumes ClientRulesSession core above; the Qt6 host/probe remains
    # outside the legacy product graph.
    return()
endif()

add_executable(qsanguosha_rules_session_probe tests/client_runtime/rules-session-probe.cpp)
target_link_libraries(qsanguosha_rules_session_probe PRIVATE
    qsanguosha_rules_session "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>")
set_target_properties(qsanguosha_rules_session_probe PROPERTIES FOLDER "Tests")
if(WIN32)
    # The main-only Windows CTest handoff already stages tests/<CONFIG>/*.exe.
    # Keep this probe there too; a new top-level executable would be omitted.
    set_target_properties(qsanguosha_rules_session_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests/$<CONFIG>")
    target_link_libraries(qsanguosha_rules_session_probe PRIVATE
        "$<$<CONFIG:Release>:dbghelp>" "$<$<CONFIG:Release>:user32>" "$<$<CONFIG:Release>:gdi32>")
endif()
if(MSVC)
    target_compile_options(qsanguosha_rules_session_probe PRIVATE /utf-8 /bigobj)
    target_compile_definitions(qsanguosha_rules_session_probe PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME qsanguosha_rules_session_lifecycle
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/client_runtime/check-rules-session.py"
            --native-only --native-runner "$<TARGET_FILE:qsanguosha_rules_session_probe>"
            --asset-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/rules-session")
    set_tests_properties(qsanguosha_rules_session_lifecycle PROPERTIES
        LABELS "client;client-runtime" TIMEOUT 240 RUN_SERIAL TRUE)
    if(WIN32)
        set_property(TEST qsanguosha_rules_session_lifecycle PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()

# First W3 slice: the same raw-frame stream implementation is reachable from
# native verification and the production WASM module. No frontend links here.
add_executable(qsanguosha_rules_ingress_probe tests/client_runtime/rules-ingress-probe.cpp)
target_link_libraries(qsanguosha_rules_ingress_probe PRIVATE
    qsanguosha_rules_session "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>")
set_target_properties(qsanguosha_rules_ingress_probe PROPERTIES FOLDER "Tests")
if(WIN32)
    set_target_properties(qsanguosha_rules_ingress_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests/$<CONFIG>")
    target_link_libraries(qsanguosha_rules_ingress_probe PRIVATE
        "$<$<CONFIG:Release>:dbghelp>" "$<$<CONFIG:Release>:user32>" "$<$<CONFIG:Release>:gdi32>")
endif()
if(MSVC)
    target_compile_options(qsanguosha_rules_ingress_probe PRIVATE /utf-8 /bigobj)
endif()
if(BUILD_TESTING)
    add_test(NAME qsanguosha_rules_ingress
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/client_runtime/check-rules-ingress.py"
            --native-runner "$<TARGET_FILE:qsanguosha_rules_ingress_probe>"
            --asset-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --artifacts "${CMAKE_CURRENT_BINARY_DIR}/rules-ingress")
    set_tests_properties(qsanguosha_rules_ingress PROPERTIES
        LABELS "client;client-runtime" TIMEOUT 240 RUN_SERIAL TRUE)
    if(WIN32)
        set_property(TEST qsanguosha_rules_ingress PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
