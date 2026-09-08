include_guard(GLOBAL)

# Source identity must be shared by native/WASM builds, independent of checkout
# path, timestamps, compiler output, and git availability. Hash actual source
# bytes (including dirty worktrees), not an unverified commit/version string.
set(qsan_identity_root "${CMAKE_CURRENT_SOURCE_DIR}")
set(qsan_identity_sources)
foreach(directory core server package scenario util lua client)
    file(GLOB_RECURSE files CONFIGURE_DEPENDS
        "${qsan_identity_root}/src/${directory}/*.cpp"
        "${qsan_identity_root}/src/${directory}/*.h"
        "${qsan_identity_root}/src/${directory}/*.hpp"
        "${qsan_identity_root}/src/${directory}/*.cxx"
        "${qsan_identity_root}/src/${directory}/*.inc"
        "${qsan_identity_root}/src/${directory}/*.c")
    list(APPEND qsan_identity_sources ${files})
endforeach()
file(GLOB qsan_identity_recipes CONFIGURE_DEPENDS "${qsan_identity_root}/cmake/*.cmake")
list(APPEND qsan_identity_sources ${qsan_identity_recipes} "${qsan_identity_root}/CMakeLists.txt")
file(GLOB qsan_identity_bindings CONFIGURE_DEPENDS "${qsan_identity_root}/swig/*.i")
list(APPEND qsan_identity_sources ${qsan_identity_bindings}
    "${qsan_identity_root}/src/client/interaction-reply-encoder.cpp"
    "${qsan_identity_root}/src/client/interaction-reply-encoder.h")
list(REMOVE_DUPLICATES qsan_identity_sources)
list(SORT qsan_identity_sources)
list(SORT qsan_identity_bindings)
function(qsan_identity_hash output)
    set(entries "qsanguosha-source-identity-v1\n")
    foreach(path IN LISTS ARGN)
        file(RELATIVE_PATH relative "${qsan_identity_root}" "${path}")
        file(READ "${path}" source)
        # C/C++ translation normalizes line endings; Windows checkouts must
        # not acquire a different source identity for CRLF alone.
        string(REPLACE "\r\n" "\n" source "${source}")
        string(SHA256 digest "${source}")
        string(APPEND entries "${relative}\t${digest}\n")
    endforeach()
    string(SHA256 digest "${entries}")
    set(${output} "${digest}" PARENT_SCOPE)
endfunction()
qsan_identity_hash(qsan_rules_source_sha ${qsan_identity_sources})
qsan_identity_hash(qsan_rules_bindings_sha ${qsan_identity_bindings})
execute_process(COMMAND "${QSAN_SWIG_EXECUTABLE}" -version
    OUTPUT_VARIABLE qsan_identity_swig_output RESULT_VARIABLE qsan_identity_swig_status)
string(REGEX MATCH "SWIG Version ([0-9]+\\.[0-9]+\\.[0-9]+)" qsan_identity_swig_match "${qsan_identity_swig_output}")
if(NOT qsan_identity_swig_status EQUAL 0 OR NOT qsan_identity_swig_match)
    message(FATAL_ERROR "Cannot identify the SWIG bindings generator")
endif()
string(SHA256 qsan_rules_bindings_sha "${qsan_rules_bindings_sha}\nSWIG=${CMAKE_MATCH_1}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${qsan_identity_sources})
set(qsan_identity_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/rules-identity")
file(MAKE_DIRECTORY "${qsan_identity_generated}")
set(qsan_identity_header "${qsan_identity_generated}/rules-source-identity.h")
set(qsan_identity_text "#pragma once\n#define QSAN_RULES_SOURCE_SHA256 \"${qsan_rules_source_sha}\"\n#define QSAN_RULES_BINDINGS_SHA256 \"${qsan_rules_bindings_sha}\"\n")
# Avoid needless recompiles on every configure.
file(CONFIGURE OUTPUT "${qsan_identity_header}" CONTENT "${qsan_identity_text}" @ONLY)

function(qsan_finalize_rules_identity)
    target_sources(qsanguosha_engine PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/rules-bundle-identity.cpp")
    target_include_directories(qsanguosha_engine PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/rules-identity")
    if(QSAN_BUILD_RULES_SESSION_TESTS AND NOT EMSCRIPTEN AND NOT QSAN_BUILD_XP_LEGACY)
        add_executable(qsanguosha_rules_identity_probe
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/client_runtime/rules-identity-probe.cpp")
        target_link_libraries(qsanguosha_rules_identity_probe PRIVATE
            qsanguosha_client_runtime "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>")
        set_target_properties(qsanguosha_rules_identity_probe PROPERTIES FOLDER "Tests")
        if(WIN32)
            set_target_properties(qsanguosha_rules_identity_probe PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests/$<CONFIG>")
            target_link_libraries(qsanguosha_rules_identity_probe PRIVATE
                "$<$<CONFIG:Release>:dbghelp>" "$<$<CONFIG:Release>:user32>" "$<$<CONFIG:Release>:gdi32>")
        endif()
        if(MSVC)
            target_compile_options(qsanguosha_rules_identity_probe PRIVATE /utf-8 /bigobj)
            target_compile_definitions(qsanguosha_rules_identity_probe PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
        if(BUILD_TESTING)
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            add_test(NAME qsanguosha_rules_bundle_identity
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/client_runtime/check-rules-identity.py"
                    --runner "$<TARGET_FILE:qsanguosha_rules_identity_probe>"
                    --asset-root "${CMAKE_CURRENT_SOURCE_DIR}"
                    --artifacts "${CMAKE_CURRENT_BINARY_DIR}/rules-identity")
            set_tests_properties(qsanguosha_rules_bundle_identity PROPERTIES
                LABELS "client;client-runtime;protocol" TIMEOUT 360 RUN_SERIAL TRUE)
            if(WIN32)
                set_property(TEST qsanguosha_rules_bundle_identity PROPERTY ENVIRONMENT_MODIFICATION
                    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
            endif()
        endif()
    endif()
endfunction()
cmake_language(DEFER CALL qsan_finalize_rules_identity)
