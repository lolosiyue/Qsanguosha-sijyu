include_guard(GLOBAL)

# This module is entered only for the explicit fixture-only cross build. It
# does not change the normal native graph and does not define a Web product.
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "QSAN_BUILD_WASM_RULES_FIXTURES requires the Emscripten toolchain")
endif()
if(BUILD_TESTING OR QSAN_BUILD_GUI OR QSAN_BUILD_SERVER OR QSAN_BUILD_TUI
        OR QSAN_BUILD_RULES_FIXTURE_RUNNER OR QSAN_BUILD_XP_LEGACY)
    message(FATAL_ERROR
        "WASM fixtures require BUILD_TESTING, GUI, server, TUI, native runner and XP options OFF")
endif()
if(NOT Qt6_VERSION VERSION_EQUAL "6.11.1")
    message(FATAL_ERROR "The initial WASM fixture probe is pinned to Qt 6.11.1")
endif()
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --version
    OUTPUT_VARIABLE qsan_wasm_compiler_version RESULT_VARIABLE qsan_wasm_compiler_status)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" qsan_wasm_emscripten_version
    "${qsan_wasm_compiler_version}")
if(NOT qsan_wasm_compiler_status EQUAL 0 OR NOT qsan_wasm_emscripten_version STREQUAL "4.0.7")
    message(FATAL_ERROR "Qt 6.11.1 WASM fixtures require Emscripten 4.0.7")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Only this fixture build uses a filename-based INI in the host's fresh MEMFS.
set_property(SOURCE src/core/settings.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS QSAN_WASM_RULES_FIXTURES)
set(qsan_wasm_fixture_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests/client_runtime")
set(qsan_wasm_assets "${CMAKE_CURRENT_BINARY_DIR}/rules-wasm-builtin")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${qsan_wasm_fixture_dir}/check-wasm-fixtures.py"
        --prepare-assets --asset-root "${CMAKE_CURRENT_SOURCE_DIR}"
        --artifacts "${qsan_wasm_assets}"
    RESULT_VARIABLE qsan_wasm_assets_status
    OUTPUT_VARIABLE qsan_wasm_assets_stdout ERROR_VARIABLE qsan_wasm_assets_stderr
)
if(NOT qsan_wasm_assets_status EQUAL 0)
    message(FATAL_ERROR "Cannot stage WASM fixture assets: ${qsan_wasm_assets_stderr}")
endif()
file(READ "${qsan_wasm_assets}/fixture-assets.json" qsan_wasm_manifest)
string(JSON qsan_wasm_asset_count LENGTH "${qsan_wasm_manifest}" files)
math(EXPR qsan_wasm_last_asset "${qsan_wasm_asset_count} - 1")
set(qsan_wasm_asset_dependencies
    "${qsan_wasm_fixture_dir}/check-fixtures.py"
    "${qsan_wasm_fixture_dir}/check-wasm-fixtures.py"
)
foreach(index RANGE ${qsan_wasm_last_asset})
    string(JSON relative GET "${qsan_wasm_manifest}" files ${index} path)
    list(APPEND qsan_wasm_asset_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/${relative}")
endforeach()
# Re-stage/relink after any bootstrap byte or closure-list change. Never embed
# the checkout directory: ignored extensions/AI and user files stay outside.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${qsan_wasm_asset_dependencies})

add_executable(qsanguosha_rules_fixture_wasm
    "${qsan_wasm_fixture_dir}/selection-fixture-main.cpp"
    "${qsan_wasm_fixture_dir}/selection-fixture-wasm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/interaction-reply-encoder.cpp"
)
target_compile_definitions(qsanguosha_rules_fixture_wasm PRIVATE main=qsan_fixture_main)
target_link_libraries(qsanguosha_rules_fixture_wasm PRIVATE
    qsanguosha_rules_fixture_support
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
)
set_target_properties(qsanguosha_rules_fixture_wasm PROPERTIES
    FOLDER "Tests"
    SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rules-wasm/$<CONFIG>"
)
set_property(TARGET qsanguosha_rules_fixture_wasm APPEND PROPERTY LINK_DEPENDS
    ${qsan_wasm_asset_dependencies} "${qsan_wasm_assets}/fixture-assets.json")
target_link_options(qsanguosha_rules_fixture_wasm PRIVATE
    --no-entry
    --bind
    # Keep function names for traps without carrying full-engine DWARF through
    # wasm-opt, which used about 9 GiB in the initial local link.
    "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-g0>"
    "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-g2>"
    -fexceptions
    -sDISABLE_EXCEPTION_CATCHING=0
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createQSanguoshaRulesFixture
    -sENVIRONMENT=node
    -sEXIT_RUNTIME=0
    -sFORCE_FILESYSTEM=1
    "-sEXPORTED_FUNCTIONS=['_qsan_run_fixture']"
    "-sEXPORTED_RUNTIME_METHODS=['FS','ENV']"
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=8388608
    -sINITIAL_MEMORY=134217728
    -sERROR_ON_UNDEFINED_SYMBOLS=1
    "SHELL:--embed-file \"${qsan_wasm_assets}@/assets\""
)
add_custom_command(TARGET qsanguosha_rules_fixture_wasm POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${qsan_wasm_assets}/fixture-assets.json"
        "$<TARGET_FILE_DIR:qsanguosha_rules_fixture_wasm>/qsanguosha_rules_fixture_wasm.assets.json"
    VERBATIM
)

# Engine/ClientCore are declared after the source inventory includes us. Apply
# the same exception model to every application-owned TU once targets exist;
# do not patch third-party Qt/Lua/SWIG or suppress unresolved engine symbols.
function(qsan_finalize_wasm_fixture_options)
    foreach(target qsanguosha_engine qsanguosha_client_core qsanguosha_client_runtime
            qsanguosha_rules_fixture_support qsanguosha_rules_fixture_wasm)
        if(NOT TARGET ${target})
            message(FATAL_ERROR "WASM fixture dependency was not declared: ${target}")
        endif()
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fexceptions>")
    endforeach()
endfunction()
cmake_language(DEFER CALL qsan_finalize_wasm_fixture_options)

# Explicit browser verification target; not part of the default Node build.
include("${CMAKE_CURRENT_LIST_DIR}/QSanguoshaRulesWorker.cmake")
