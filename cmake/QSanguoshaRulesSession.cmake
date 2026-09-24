include_guard(GLOBAL)

if(NOT QSAN_BUILD_WASM_WEB_CLIENT AND NOT QSAN_BUILD_EXCEL AND NOT QSAN_BUILD_SHEETS)
    return()
endif()

# One production implementation; final executables retain responsibility for WHOLE_ARCHIVE engine linkage.
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
