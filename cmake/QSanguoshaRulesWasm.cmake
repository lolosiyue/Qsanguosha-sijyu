include_guard(GLOBAL)

# The production Worker targets share the cross-build and exception recipe.
# Native products never enter this module.
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "WASM rules targets require the Emscripten toolchain")
endif()
if(QSAN_BUILD_GUI OR QSAN_BUILD_SERVER OR QSAN_BUILD_TUI OR QSAN_BUILD_XP_LEGACY)
    message(FATAL_ERROR
        "WASM rules targets require GUI, server, TUI and XP options OFF")
endif()
if(NOT Qt6_VERSION VERSION_EQUAL "6.11.1")
    message(FATAL_ERROR "The WASM rules runtime is pinned to Qt 6.11.1")
endif()
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --version
    OUTPUT_VARIABLE qsan_wasm_compiler_version RESULT_VARIABLE qsan_wasm_compiler_status)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" qsan_wasm_emscripten_version
    "${qsan_wasm_compiler_version}")
if(NOT qsan_wasm_compiler_status EQUAL 0 OR NOT qsan_wasm_emscripten_version STREQUAL "4.0.7")
    message(FATAL_ERROR "Qt 6.11.1 WASM rules targets require Emscripten 4.0.7")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Both hosts use a filename-based INI inside their isolated MEMFS.
set_property(SOURCE src/core/settings.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS QSAN_WASM_CLIENT_RUNTIME)
function(qsan_configure_wasm_rules_module target factory environment exports)
    target_link_options(${target} PRIVATE
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
        "-sEXPORT_NAME=${factory}"
        "-sENVIRONMENT=${environment}"
        -sEXIT_RUNTIME=0
        -sFORCE_FILESYSTEM=1
        "-sEXPORTED_FUNCTIONS=${exports}"
        "-sEXPORTED_RUNTIME_METHODS=['FS','ENV']"
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=8388608
        -sINITIAL_MEMORY=134217728
        -sERROR_ON_UNDEFINED_SYMBOLS=1
    )
endfunction()

# Engine/ClientCore are declared after the source inventory includes us. Apply
# the same exception model to every application-owned TU once targets exist;
# do not patch third-party Qt/Lua/SWIG or suppress unresolved engine symbols.
function(qsan_finalize_wasm_rules_options)
    set(targets qsanguosha_engine qsanguosha_client_core qsanguosha_client_runtime)
    if(QSAN_BUILD_WASM_WEB_CLIENT)
        list(APPEND targets qsanguosha_client_wasm)
    endif()
    if(QSAN_BUILD_WASM_SOLO)
        list(APPEND targets qsanguosha_solo_wasm)
    endif()
    foreach(target IN LISTS targets)
        if(NOT TARGET ${target})
            message(FATAL_ERROR "WASM rules dependency was not declared: ${target}")
        endif()
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fexceptions>")
    endforeach()
endfunction()
cmake_language(DEFER CALL qsan_finalize_wasm_rules_options)

