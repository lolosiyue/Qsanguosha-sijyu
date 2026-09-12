include_guard(GLOBAL)

if(NOT QSAN_BUILD_EXCEL)
    message(FATAL_ERROR "QSAN_BUILD_SHEETS requires QSAN_BUILD_EXCEL=ON for the shared native bridge/helper")
endif()
if(QSAN_BUILD_XP_LEGACY)
    message(FATAL_ERROR "QSAN_BUILD_SHEETS currently requires the modern Windows bridge")
endif()

# Sheets uses the proven Excel bridge/session facade with a different launcher.
# It deliberately omits ExcelProcessGuard: stdin EOF is the sole owner signal.
set(qsan_sheets_sources
    src/sheets/sheets-main.cpp
    src/excel/excel-bridge.cpp src/excel/excel-bridge.h
    src/excel/excel-interaction.cpp src/excel/excel-interaction.h
    src/excel/excel-ipc-server.cpp src/excel/excel-ipc-server.h
    src/excel/excel-view.cpp src/excel/excel-view.h
    legacy/xp/src/local-server-controller.cpp
    legacy/xp/src/local-server-controller.h
    src/server/server-config.cpp src/server/server-config.h)

add_executable(qsanguosha_sheets_bridge ${qsan_sheets_sources})
set_target_properties(qsanguosha_sheets_bridge PROPERTIES
    OUTPUT_NAME QSanguoshaSheetsBridge FOLDER "Clients"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/excel-debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/excel-release")
target_include_directories(qsanguosha_sheets_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client ${CMAKE_CURRENT_SOURCE_DIR}/src/client/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/runtime ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/src)
target_compile_definitions(qsanguosha_sheets_bridge PRIVATE
    QSAN_EXCEL_BRIDGE QSAN_ENGINE_BUILD QSAN_SERVER_CORE_ONLY
    AUDIO_SUPPORT QSAN_AUDIO_BACKEND_NAME="${QSAN_AUDIO_BACKEND}"
    QSAN_XP_BUILD_ID="${QSAN_XP_BUILD_ID}-$<CONFIG>")
target_link_libraries(qsanguosha_sheets_bridge PRIVATE
    qsanguosha_client_runtime qsanguosha_rules_session qsanguosha_client_session
    ${QSAN_ENGINE_QT_LIBS})
target_sources(qsanguosha_sheets_bridge PRIVATE
    src/core/audio.h src/ui/audio/audio.cpp src/ui/audio/audio-backend.h
    src/ui/audio/audio-backend-factory.cpp src/ui/audio/null-audio-backend.cpp
    src/ui/audio/null-audio-backend.h)
if(QSAN_AUDIO_BACKEND STREQUAL "QT")
    target_sources(qsanguosha_sheets_bridge PRIVATE src/ui/audio/qt-audio-backend.cpp src/ui/audio/qt-audio-backend.h)
    target_compile_definitions(qsanguosha_sheets_bridge PRIVATE QSAN_AUDIO_BACKEND_QT)
    target_link_libraries(qsanguosha_sheets_bridge PRIVATE Qt6::Multimedia)
elseif(QSAN_AUDIO_BACKEND STREQUAL "FMOD")
    target_sources(qsanguosha_sheets_bridge PRIVATE src/ui/audio/fmod-audio-backend.cpp src/ui/audio/fmod-audio-backend.h)
    target_compile_definitions(qsanguosha_sheets_bridge PRIVATE QSAN_AUDIO_BACKEND_FMOD)
    target_include_directories(qsanguosha_sheets_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/fmod)
    target_link_libraries(qsanguosha_sheets_bridge PRIVATE
        "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x64/fmodexL.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x64/fmodex.lib>")
endif()
if(MSVC)
    target_compile_definitions(qsanguosha_sheets_bridge PRIVATE WIN64 _CRT_SECURE_NO_WARNINGS)
    target_compile_options(qsanguosha_sheets_bridge PRIVATE /utf-8 /bigobj /MP8)
endif()
target_link_libraries(qsanguosha_sheets_bridge PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>")
if(TARGET qsanguosha_excel_control)
    target_link_libraries(qsanguosha_sheets_bridge PRIVATE qsanguosha_excel_control)
elseif(TARGET qsan_xp_control)
    target_link_libraries(qsanguosha_sheets_bridge PRIVATE qsan_xp_control)
endif()
add_dependencies(qsanguosha_sheets_bridge qsanguosha_excel_server)
target_link_libraries(qsanguosha_sheets_bridge PRIVATE Ws2_32 IPHLPAPI)
