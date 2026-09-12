include_guard(GLOBAL)

if(QSAN_BUILD_XP_LEGACY AND QSAN_AUDIO_BACKEND STREQUAL "QT")
    message(FATAL_ERROR "Excel Qt5/XP supports FMOD or NULL audio; QT audio requires Qt6")
endif()

# Excel is an adapter product. Keep live-session, rules, and presentation
# layers explicit so this target never acquires the TUI implementation.
set(qsan_excel_sources
    src/excel/excel-main.cpp
    src/excel/excel-bridge.cpp src/excel/excel-bridge.h
    src/excel/excel-interaction.cpp src/excel/excel-interaction.h
    src/excel/excel-ipc-server.cpp src/excel/excel-ipc-server.h
    src/excel/excel-process-guard.cpp src/excel/excel-process-guard.h
    src/excel/excel-view.cpp src/excel/excel-view.h
    legacy/xp/src/local-server-controller.cpp
    legacy/xp/src/local-server-controller.h
    src/server/server-config.cpp src/server/server-config.h)

add_executable(qsanguosha_excel_bridge ${qsan_excel_sources})
set_target_properties(qsanguosha_excel_bridge PROPERTIES
    OUTPUT_NAME QSanguoshaExcelBridge FOLDER "Clients")
target_include_directories(qsanguosha_excel_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client ${CMAKE_CURRENT_SOURCE_DIR}/src/client/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/runtime ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/src)
target_compile_definitions(qsanguosha_excel_bridge PRIVATE
    QSAN_EXCEL_BRIDGE QSAN_ENGINE_BUILD QSAN_SERVER_CORE_ONLY
    AUDIO_SUPPORT QSAN_AUDIO_BACKEND_NAME="${QSAN_AUDIO_BACKEND}")
target_link_libraries(qsanguosha_excel_bridge PRIVATE
    qsanguosha_client_runtime qsanguosha_rules_session qsanguosha_client_session
    ${QSAN_ENGINE_QT_LIBS})

# The bridge owns the facade and selects the configured backend, including NULL.
target_sources(qsanguosha_excel_bridge PRIVATE
    src/core/audio.h src/ui/audio/audio.cpp src/ui/audio/audio-backend.h
    src/ui/audio/audio-backend-factory.cpp src/ui/audio/null-audio-backend.cpp
    src/ui/audio/null-audio-backend.h)
if(QSAN_AUDIO_BACKEND STREQUAL "QT")
    target_sources(qsanguosha_excel_bridge PRIVATE
        src/ui/audio/qt-audio-backend.cpp src/ui/audio/qt-audio-backend.h)
    target_compile_definitions(qsanguosha_excel_bridge PRIVATE QSAN_AUDIO_BACKEND_QT)
    target_link_libraries(qsanguosha_excel_bridge PRIVATE Qt6::Multimedia)
elseif(QSAN_AUDIO_BACKEND STREQUAL "FMOD")
    target_include_directories(qsanguosha_excel_bridge PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/fmod)
    if(QSAN_BUILD_XP_LEGACY)
        target_sources(qsanguosha_excel_bridge PRIVATE
            src/ui/audio/fmod-audio-backend.cpp src/ui/audio/fmod-audio-backend.h)
        target_compile_definitions(qsanguosha_excel_bridge PRIVATE QSAN_AUDIO_BACKEND_FMOD)
    else()
        target_sources(qsanguosha_excel_bridge PRIVATE
            src/ui/audio/fmod-audio-backend.cpp
            src/ui/audio/fmod-audio-backend.h)
        target_compile_definitions(qsanguosha_excel_bridge PRIVATE
            QSAN_AUDIO_BACKEND_FMOD)
    endif()
    if(QSAN_BUILD_XP_LEGACY)
        target_link_libraries(qsanguosha_excel_bridge PRIVATE
            "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x86/fmodexL.lib>"
            "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x86/fmodex.lib>")
    else()
        target_link_libraries(qsanguosha_excel_bridge PRIVATE
            "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x64/fmodexL.lib>"
            "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x64/fmodex.lib>")
    endif()
endif()

if(QSAN_BUILD_XP_LEGACY)
    # qsan_xp_target supplies the complete Qt5/v141_xp ABI and 5.01 subsystem.
    target_include_directories(qsanguosha_excel_bridge BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/src)
    qsan_xp_target(qsanguosha_excel_bridge)
    target_link_libraries(qsanguosha_excel_bridge PRIVATE
        qsan_xp_control qsanguosha_engine Ws2_32 IPHLPAPI dbghelp user32 gdi32)
    target_link_options(qsanguosha_excel_bridge PRIVATE
        "/WHOLEARCHIVE:$<TARGET_FILE:qsanguosha_engine>")
else()
    # Modern helper uses the same entrypoint and dedicated source set as XP,
    # with a paired non-empty identity and a separate Qt6 output tree.
    set(qsan_excel_identity_inputs
        CMakeLists.txt cmake/QSanguoshaExcel.cmake
        legacy/xp/src/xp-server-main.cpp legacy/xp/src/xp-control-protocol.cpp
        ${QSAN_DEDICATED_ENTRY_SOURCES})
    set(qsan_excel_identity_hashes "")
    foreach(qsan_identity_file IN LISTS qsan_excel_identity_inputs)
        file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${qsan_identity_file}" qsan_identity_digest)
        string(APPEND qsan_excel_identity_hashes "${qsan_identity_digest}")
    endforeach()
    string(SHA256 QSAN_XP_BUILD_ID
        "${qsan_excel_identity_hashes};${CMAKE_CXX_COMPILER_VERSION};${CMAKE_GENERATOR_TOOLSET};${Qt6Core_VERSION}")
    if(QSAN_XP_BUILD_ID STREQUAL "")
        message(FATAL_ERROR "QSAN_XP_BUILD_ID must be a paired, non-empty identity")
    endif()

    add_library(qsanguosha_excel_control STATIC
        legacy/xp/src/xp-control-protocol.cpp legacy/xp/src/xp-control-protocol.h)
    target_include_directories(qsanguosha_excel_control PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/src)
    target_compile_definitions(qsanguosha_excel_control PRIVATE
        QSAN_XP_BUILD_ID="${QSAN_XP_BUILD_ID}-$<CONFIG>")
    target_link_libraries(qsanguosha_excel_control PUBLIC Qt6::Core Qt6::Network advapi32)
    set_target_properties(qsanguosha_excel_control PROPERTIES FOLDER "Libraries")
    target_link_libraries(qsanguosha_excel_bridge PRIVATE
        qsanguosha_excel_control "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>")

    add_executable(qsanguosha_excel_server
        legacy/xp/src/xp-server-main.cpp ${QSAN_DEDICATED_ENTRY_SOURCES})
    set_target_properties(qsanguosha_excel_server PROPERTIES
        OUTPUT_NAME QSanguoshaExcelServer FOLDER "Servers"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/excel-debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/excel-release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/excel-relwithdebinfo")
    target_include_directories(qsanguosha_excel_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/src)
    target_compile_definitions(qsanguosha_excel_server PRIVATE
        QSAN_ENGINE_BUILD QSAN_SERVER_CORE_ONLY
        QSAN_XP_BUILD_ID="${QSAN_XP_BUILD_ID}-$<CONFIG>" WIN64 _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(qsanguosha_excel_server PRIVATE QSAN_MANAGED_SERVER_ENTRY)
    target_compile_options(qsanguosha_excel_server PRIVATE /utf-8 /bigobj /MP8)
    target_link_libraries(qsanguosha_excel_server PRIVATE
        qsanguosha_excel_control Ws2_32 IPHLPAPI
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>" ${QSAN_ENGINE_QT_LIBS})
    qsan_link_websocket_gateway(qsanguosha_excel_server)
endif()

if(MSVC AND NOT QSAN_BUILD_XP_LEGACY)
    target_compile_definitions(qsanguosha_excel_bridge PRIVATE WIN64 _CRT_SECURE_NO_WARNINGS)
    target_compile_options(qsanguosha_excel_bridge PRIVATE /utf-8 /bigobj /MP8)
endif()

if(QSAN_BUILD_XP_LEGACY)
    set(qsan_excel_bridge_debug "${CMAKE_CURRENT_SOURCE_DIR}/xp-debug")
    set(qsan_excel_bridge_release "${CMAKE_CURRENT_SOURCE_DIR}/xp-release")
else()
    set(qsan_excel_bridge_debug "${CMAKE_CURRENT_SOURCE_DIR}/excel-debug")
    set(qsan_excel_bridge_release "${CMAKE_CURRENT_SOURCE_DIR}/excel-release")
endif()
set_target_properties(qsanguosha_excel_bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${qsan_excel_bridge_debug}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${qsan_excel_bridge_release}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${qsan_excel_bridge_release}")

# Focused native consumers are intentionally buildable but are not registered
# with CTest; local policy runs them explicitly after a user-approved checkpoint.
if(QSAN_BUILD_EXCEL_TESTS)
    add_executable(qsanguosha_excel_ipc_tests
        tests/excel/ipc-test.cpp src/excel/excel-ipc-server.cpp
        src/excel/excel-ipc-server.h)
    target_compile_definitions(qsanguosha_excel_ipc_tests PRIVATE
        QSANGUOSHA_EXCEL_IPC_TEST)
    target_link_libraries(qsanguosha_excel_ipc_tests PRIVATE Qt6::Core Qt6::Network)

    add_executable(qsanguosha_excel_interaction_tests
        tests/excel/interaction-test.cpp src/excel/excel-interaction.cpp
        src/excel/excel-interaction.h)
    target_link_libraries(qsanguosha_excel_interaction_tests PRIVATE
        qsanguosha_client_core qsanguosha_client_runtime qsanguosha_rules_session
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
        Qt6::Core Qt6::Network Qt6::Test)

    add_executable(qsanguosha_excel_view_tests
        tests/excel/view-test.cpp src/excel/excel-view.cpp src/excel/excel-view.h
        src/server/server-config.cpp src/server/server-config.h
        src/core/audio.h src/ui/audio/audio.cpp src/ui/audio/audio-backend.h
        src/ui/audio/audio-backend-factory.cpp src/ui/audio/null-audio-backend.cpp
        src/ui/audio/null-audio-backend.h)
    target_link_libraries(qsanguosha_excel_view_tests PRIVATE
        qsanguosha_client_core qsanguosha_client_runtime
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
        Qt6::Core Qt6::Network Qt6::Test)
    # Consumers must use the engine's Settings layout, not the GUI variant.
    foreach(qsan_excel_engine_test IN ITEMS qsanguosha_excel_interaction_tests
        qsanguosha_excel_view_tests)
        target_compile_definitions(${qsan_excel_engine_test} PRIVATE
            QSAN_ENGINE_BUILD QSAN_SERVER_CORE_ONLY)
    endforeach()
    set_target_properties(qsanguosha_excel_ipc_tests
        qsanguosha_excel_interaction_tests qsanguosha_excel_view_tests PROPERTIES
        FOLDER "Tests")
    foreach(qsan_excel_test IN ITEMS qsanguosha_excel_ipc_tests
        qsanguosha_excel_interaction_tests qsanguosha_excel_view_tests)
        target_include_directories(${qsan_excel_test} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/client ${CMAKE_CURRENT_SOURCE_DIR}/src/client/core
            ${CMAKE_CURRENT_SOURCE_DIR}/src/client/runtime ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${CMAKE_CURRENT_SOURCE_DIR}/src/server ${CMAKE_CURRENT_SOURCE_DIR}/src/util)
    endforeach()
    if(MSVC)
        foreach(qsan_excel_test IN ITEMS qsanguosha_excel_ipc_tests
            qsanguosha_excel_interaction_tests qsanguosha_excel_view_tests)
            target_compile_options(${qsan_excel_test} PRIVATE /utf-8 /bigobj)
        endforeach()
    endif()
endif()

if(WIN32 AND NOT QSAN_BUILD_XP_LEGACY)
    foreach(qsan_excel_binary IN ITEMS qsanguosha_excel_bridge qsanguosha_excel_server
        qsanguosha_excel_interaction_tests qsanguosha_excel_view_tests)
        if(TARGET ${qsan_excel_binary})
            target_link_libraries(${qsan_excel_binary} PRIVATE
                "$<$<CONFIG:Release>:dbghelp>"
                "$<$<CONFIG:Release>:user32>"
                "$<$<CONFIG:Release>:gdi32>")
        endif()
    endforeach()
endif()
