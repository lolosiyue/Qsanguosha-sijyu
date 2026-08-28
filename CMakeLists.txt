cmake_minimum_required(VERSION 3.28)

project(QSanguosha LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)

# 產品選項：GUI client 與 dedicated server 各自獨立，平台只決定預設值，
# 不再由平台永久決定產品是否存在。
if(WIN32)
    set(QSAN_BUILD_GUI_DEFAULT ON)
else()
    set(QSAN_BUILD_GUI_DEFAULT OFF)
endif()

option(QSAN_BUILD_GUI
    "Build the QSanguosha GUI client"
    ${QSAN_BUILD_GUI_DEFAULT}
)
option(QSAN_BUILD_SERVER
    "Build the dedicated QSanguosha server"
    ON
)

# Audio backend。GUI client 用邊個實作由呢個變數單點決定，call site 冇任何
# 平台 #ifdef：
#
#   FMOD  Windows GUI Release（bundled fmodex，行為同以前一樣）
#   QT    Linux GUI（Qt Multimedia）
#   NULL  dedicated server／CI／任何唔想出聲的 build
#
# dedicated server 唔會編入任何 audio 檔案，所以 NULL 亦係 server-only build
# 的自然預設。
if(WIN32)
    set(QSAN_AUDIO_BACKEND_DEFAULT "FMOD")
elseif(QSAN_BUILD_GUI)
    set(QSAN_AUDIO_BACKEND_DEFAULT "QT")
else()
    set(QSAN_AUDIO_BACKEND_DEFAULT "NULL")
endif()
set(QSAN_AUDIO_BACKEND "${QSAN_AUDIO_BACKEND_DEFAULT}" CACHE STRING
    "Audio backend for the GUI client: FMOD | QT | NULL")
set_property(CACHE QSAN_AUDIO_BACKEND PROPERTY STRINGS FMOD QT NULL)
if(NOT QSAN_AUDIO_BACKEND MATCHES "^(FMOD|QT|NULL)$")
    message(FATAL_ERROR
        "QSAN_AUDIO_BACKEND must be one of FMOD, QT, NULL (got '${QSAN_AUDIO_BACKEND}')")
endif()
if(QSAN_AUDIO_BACKEND STREQUAL "FMOD" AND NOT WIN32)
    message(FATAL_ERROR
        "QSAN_AUDIO_BACKEND=FMOD is Windows-only: lib/win/x64/fmodex.lib and include/fmod "
        "are Windows binaries and must never be linked or included on other platforms")
endif()

if(BUILD_TESTING AND NOT QSAN_BUILD_SERVER)
    message(FATAL_ERROR
        "BUILD_TESTING=ON requires QSAN_BUILD_SERVER=ON (CTest drives qsanguosha_server)")
endif()

if(WIN32)
    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The Windows CMake build supports x64 MSVC only")
    endif()
    if(MSVC_VERSION LESS 1950)
        message(FATAL_ERROR "The Windows CMake build supports the Visual Studio 2026 v145 toolchain only")
    endif()
elseif(NOT UNIX)
    message(FATAL_ERROR "Unsupported platform")
endif()

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 單一 Qt component 清單，按產品分層，避免 Windows／Linux 各自維護會漂移的兩份清單。
set(QSAN_QT_COMPONENTS
    Core
    Network
)

if(BUILD_TESTING)
    list(APPEND QSAN_QT_COMPONENTS
        Core5Compat
        Gui
    )
endif()

if(QSAN_BUILD_GUI)
    list(APPEND QSAN_QT_COMPONENTS
        Core5Compat
        Gui
        LinguistTools
        Multimedia
        OpenGLWidgets
        Qml
        Quick
        QuickControls2
        QuickWidgets
        Widgets
    )
endif()

list(REMOVE_DUPLICATES QSAN_QT_COMPONENTS)

# GUI 產品的 Qt baseline 在 Windows 與 Linux 保持一致。GUI source 用到
# Qt 6.5 才加入的 API（例如 QStyleHints::colorScheme()），且 Windows 正式
# toolchain 已經係 Qt 6.11.1，所以 GUI build 明確要求 6.11。
#
# server-only build 刻意唔套用呢個下限：dedicated server 只需要 Core／Network，
# 要繼續可以用 distro 提供的 Qt 6.x（例如 Ubuntu 24.04 的 6.4.2）建置。
set(QSAN_QT_GUI_MINIMUM_VERSION 6.11)

if(QSAN_BUILD_GUI OR WIN32)
    set(qsan_qt_minimum_version ${QSAN_QT_GUI_MINIMUM_VERSION})
else()
    set(qsan_qt_minimum_version "")
endif()

find_package(Qt6 ${qsan_qt_minimum_version} REQUIRED COMPONENTS ${QSAN_QT_COMPONENTS})

if(QSAN_BUILD_GUI)
    message(STATUS "QSanguosha GUI Qt baseline: ${QSAN_QT_GUI_MINIMUM_VERSION} (found ${Qt6_VERSION})")
endif()

get_filename_component(QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)

find_program(QSAN_SWIG_EXECUTABLE
    NAMES swig swig.exe
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/swig"
    REQUIRED
)

file(GLOB qsan_swig_interfaces CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/swig/*.i"
)
set(qsan_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(qsan_swig_wrapper "${qsan_generated_dir}/sanguosha_wrap.cxx")
add_custom_command(
    OUTPUT "${qsan_swig_wrapper}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${qsan_generated_dir}"
    COMMAND "${QSAN_SWIG_EXECUTABLE}" -c++ -lua
        -o "${qsan_swig_wrapper}"
        "${CMAKE_CURRENT_SOURCE_DIR}/swig/sanguosha.i"
    DEPENDS ${qsan_swig_interfaces}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating C++/Lua bindings with SWIG"
    VERBATIM
)
set_source_files_properties("${qsan_swig_wrapper}" PROPERTIES GENERATED TRUE)

include(cmake/QSanguoshaSources.cmake)

set(QSAN_ENGINE_PACKAGE_SOURCES ${QSAN_SOURCES})
list(FILTER QSAN_ENGINE_PACKAGE_SOURCES INCLUDE REGEX "^src/(package|scenario)/.*\\.cpp$")
list(REMOVE_ITEM QSAN_SOURCES ${QSAN_ENGINE_PACKAGE_SOURCES})
set(QSAN_ENGINE_PACKAGE_MOC_HEADERS ${QSAN_MOC_HEADERS})
list(FILTER QSAN_ENGINE_PACKAGE_MOC_HEADERS INCLUDE REGEX "^src/(package|scenario)/.*\\.h$")
list(REMOVE_ITEM QSAN_MOC_HEADERS ${QSAN_ENGINE_PACKAGE_MOC_HEADERS})

set_source_files_properties(src/server/server.cpp PROPERTIES
    COMPILE_DEFINITIONS QSAN_SERVER_CORE_ONLY
    SKIP_AUTOMOC ON
)

add_library(qsanguosha_engine STATIC
    src/core/banpair.cpp
    src/core/banpair.h
    src/core/card.cpp
    src/core/card-lifetime-manager.cpp
    src/core/card-lifetime-manager.h
    src/core/card.h
    src/core/ai-data-store.cpp
    src/core/ai-data-store.h
    src/core/engine.cpp
    src/core/engine.h
    src/core/version.h
    src/core/engine-bootstrap.cpp
    src/core/engine-bootstrap.h
    src/core/general.cpp
    src/core/general.h
    src/core/game-rng.cpp
    src/core/game-rng.h
    src/core/lua-runtime.cpp
    src/core/lua-runtime.h
    src/core/lua-wrapper.cpp
    src/core/lua-wrapper.h
    src/core/filehandler.cpp
    src/core/filehandler.h
    src/core/json.cpp
    src/core/player.cpp
    src/core/player-self.cpp
    src/core/player.h
    src/core/protocol.cpp
    src/core/protocol/card-provenance-message.cpp
    src/core/protocol/card-provenance-message.h
    src/core/protocol/protocol-message-utils.h
    src/core/protocol/skill-instance-message.cpp
    src/core/protocol/skill-instance-message.h
    src/core/protocol/state/player-ui-state.cpp
    src/core/protocol/state/player-ui-state.h
    src/core/protocol/switch-context-message.cpp
    src/core/protocol/switch-context-message.h
    src/core/protocol/sync-pile-message.cpp
    src/core/protocol/sync-pile-message.h
    src/core/record-buffer.cpp
    src/core/record-buffer.h
    src/core/room-definition-registry.cpp
    src/core/room-definition-registry.h
    src/core/room-state.cpp
    src/core/room-state.h
    src/core/server-info.cpp
    src/core/settings.cpp
    src/core/skill.cpp
    src/core/skill-registry.cpp
    src/core/skill-registry.h
    src/core/skill-instance-utils.cpp
    src/core/skill-instance-utils.h
    src/core/skill.h
    src/core/structs.cpp
    src/core/structs.h
    src/core/util.cpp
    src/core/util.h
    src/core/wrapped-card.cpp
    src/core/wrapped-card.h
    src/server/server.cpp
    src/server/ai.cpp
    src/server/ai-decision-coordinator.cpp
    src/server/ai-decision-coordinator.h
    src/server/ai-runtime.cpp
    src/server/ai-runtime.h
    src/server/basicai.cpp
    src/server/card-movement-service.cpp
    src/server/card-movement-service.h
    src/server/event-dispatcher.h
    src/server/extra-turn-scheduler.cpp
    src/server/extra-turn-scheduler.h
    src/server/game-snapshot-service.cpp
    src/server/game-snapshot-service.h
    src/server/gamerule.cpp
    src/server/generalselector.cpp
    src/server/qtupnpportmapping.cpp
    src/server/request-coordinator.cpp
    src/server/request-coordinator.h
    src/server/player-decision-service.cpp
    src/server/player-decision-service.h
    src/server/room-notifier.cpp
    src/server/room-notifier.h
    src/server/room-roster.cpp
    src/server/room-roster.h
    src/server/room-runtime.cpp
    src/server/room-runtime.h
    src/server/player-lifecycle-service.cpp
    src/server/player-lifecycle-service.h
    src/server/skill-runtime-coordinator.cpp
    src/server/skill-runtime-coordinator.h
    src/server/room.cpp
    src/server/roomthread.cpp
    src/server/roomthread1v1.cpp
    src/server/roomthread3v3.cpp
    src/server/roomthreadxmode.cpp
    src/server/player-ui-state-builder.cpp
    src/server/player-ui-state-builder.h
    src/server/serverplayer.cpp
    src/util/nativesocket.cpp
    src/util/nativesocket.h
    src/util/socket.h
    src/util/game-snapshot.cpp
    src/util/game-snapshot.h
    src/util/crashhandler.cpp
    src/util/crashhandler.h
    src/client/aux-skills.cpp
    src/client/aux-skills.h
    ${QSAN_ENGINE_PACKAGE_SOURCES}
    ${QSAN_LUA_SOURCES}
    "${qsan_swig_wrapper}"
)

set_source_files_properties(src/server/server-core.h PROPERTIES SKIP_AUTOMOC ON)
set_source_files_properties(${QSAN_LUA_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
list(REMOVE_ITEM QSAN_SOURCES src/util/game-snapshot.cpp)
list(REMOVE_ITEM QSAN_SOURCES src/client/aux-skills.cpp)
list(REMOVE_ITEM QSAN_MOC_HEADERS src/util/game-snapshot.h src/util/socket.h src/client/aux-skills.h)

target_include_directories(qsanguosha_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/package
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
)

target_compile_definitions(qsanguosha_engine PRIVATE
    QSAN_ENGINE_BUILD
    QSAN_SERVER_CORE_ONLY
    "$<$<CONFIG:Release>:QSG_CRASH_HANDLER>"
)

if(MSVC)
    target_compile_definitions(qsanguosha_engine PRIVATE WIN64 _CRT_SECURE_NO_WARNINGS)
    target_compile_options(qsanguosha_engine PRIVATE /utf-8 /bigobj /MP8)
endif()

target_precompile_headers(qsanguosha_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine-pch.h
)

target_link_libraries(qsanguosha_engine PUBLIC
    Qt6::Core
    Qt6::Network
)

if(WIN32)
    target_link_libraries(qsanguosha_engine PRIVATE Ws2_32 IPHLPAPI)
endif()

get_target_property(qsan_engine_links qsanguosha_engine LINK_LIBRARIES)
set(_qsan_engine_qt_allowlist Qt6::Core Qt6::Network)
foreach(qsan_engine_link IN LISTS qsan_engine_links)
    if(qsan_engine_link MATCHES "^Qt6::" AND NOT qsan_engine_link IN_LIST _qsan_engine_qt_allowlist)
        message(FATAL_ERROR "qsanguosha_engine links forbidden Qt target: ${qsan_engine_link}")
    endif()
endforeach()

if(QSAN_BUILD_SERVER)
    add_executable(qsanguosha_server
        src/server-main.cpp
        src/server/server-command-line.cpp
        src/server/server-command-line.h
        src/server/server-config.cpp
        src/server/server-config.h
        src/server/server-console.cpp
        src/server/server-console.h
        src/server/server-logger.cpp
        src/server/server-logger.h
        src/server/server-core.h
        src/server/server-status.h
    )
    set_source_files_properties(src/server/server-core.h PROPERTIES SKIP_AUTOMOC OFF)
    target_include_directories(qsanguosha_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )
    target_compile_definitions(qsanguosha_server PRIVATE
        QSAN_ENGINE_BUILD
        QSAN_SERVER_CORE_ONLY
    )
    if(MSVC)
        target_compile_definitions(qsanguosha_server PRIVATE WIN64 _CRT_SECURE_NO_WARNINGS)
    endif()
    target_link_libraries(qsanguosha_server PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
        Qt6::Core
        Qt6::Network
    )
    if(WIN32)
        target_link_libraries(qsanguosha_server PRIVATE Ws2_32 IPHLPAPI)
        # crash handler(crashhandler.cpp, Release 啟用)需要
        target_link_libraries(qsanguosha_server PRIVATE
            "$<$<CONFIG:Release>:dbghelp>"
            "$<$<CONFIG:Release>:user32>"
            "$<$<CONFIG:Release>:gdi32>"
        )
    endif()
    set_target_properties(qsanguosha_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/relwithdebinfo"
        PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/debug"
        PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/release"
        PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/relwithdebinfo"
    )
    if(MSVC)
        target_link_options(qsanguosha_server PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    endif()

    add_custom_target(deploy-server
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/lua"
            "$<TARGET_FILE_DIR:qsanguosha_server>/lua"
        DEPENDS qsanguosha_server
        VERBATIM
    )
endif()

if(UNIX AND QSAN_BUILD_SERVER)
    set(QSAN_SYSTEMD_SERVICE_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/qsanguosha-server.service")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/qsanguosha-server.service.in"
        "${QSAN_SYSTEMD_SERVICE_FILE}"
        @ONLY
    )
    install(TARGETS qsanguosha_server
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(DIRECTORY lua extensions
        DESTINATION "${CMAKE_INSTALL_DATADIR}/qsanguosha"
    )
    install(FILES docs/server.ini.example
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    )
    install(FILES docs/linux-development-environment.md
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    )
    install(FILES "${QSAN_SYSTEMD_SERVICE_FILE}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/system"
    )
endif()

if(QSAN_BUILD_GUI)
    # GUI 產品的 source／MOC header／form／QML resource／askFor runner 由 Windows
    # 與 Linux 共用；平台只追加平台專屬項目，不維護第二套 source list。
    set(QSAN_GUI_SOURCES
        ${QSAN_SOURCES}
        ${QSAN_MOC_HEADERS}
        ${QSAN_FORMS}
        src/ui/package-dialogs.cpp
        src/ui/package-dialogs.h
    )

    if(WIN32)
        list(APPEND QSAN_GUI_SOURCES
            resource/icon.rc
        )
        qt_add_executable(QSanguosha WIN32
            ${QSAN_GUI_SOURCES}
        )
    else()
        qt_add_executable(QSanguosha
            ${QSAN_GUI_SOURCES}
        )
    endif()

    target_sources(QSanguosha PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/qml/home/home_qml.qrc"
    )

    # home 介面圖示為本機資源，不入庫；本機存在 icons 目錄時才編譯進 qrc，避免 CI 缺少資源導致 RCC 錯誤
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qml/home/icons")
        target_sources(QSanguosha PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/qml/home/home_assets.qrc"
        )
    endif()

    target_sources(QSanguosha PRIVATE
        src/ui/testing/local-response-ui-case.cpp
        src/ui/testing/local-response-ui-case.h
        src/ui/testing/local-response-ui-controller.cpp
        src/ui/testing/local-response-ui-controller.h
        src/ui/testing/local-response-ui-inspector.cpp
        src/ui/testing/local-response-ui-inspector.h
        src/ui/testing/local-response-ui-probe.cpp
        src/ui/testing/local-response-ui-probe.h
        src/ui/testing/multimedia-smoke-controller.cpp
        src/ui/testing/multimedia-smoke-controller.h
        src/ui/testing/multimedia-smoke-report.cpp
        src/ui/testing/multimedia-smoke-report.h
        src/ui/testing/network-ui-smoke-controller.cpp
        src/ui/testing/network-ui-smoke-controller.h
        src/ui/testing/network-ui-smoke-report.cpp
        src/ui/testing/network-ui-smoke-report.h
        src/ui/testing/network-ui-smoke-responder.cpp
        src/ui/testing/network-ui-smoke-responder.h
        src/ui/testing/test-client-socket.cpp
        src/ui/testing/test-client-socket.h
        src/ui/testing/ui-startup-smoke-controller.cpp
        src/ui/testing/ui-startup-smoke-controller.h
        src/ui/testing/ui-startup-smoke-report.cpp
        src/ui/testing/ui-startup-smoke-report.h
    )

    # Audio facade + backend。facade（src/core/audio.h + src/ui/audio/audio.cpp）
    # 同 null backend 一定編入，所以任何 config 的 call site 都有實作可以連；
    # 真正的 FMOD／Qt 實作先至按 QSAN_AUDIO_BACKEND 加入。
    target_sources(QSanguosha PRIVATE
        src/core/audio.h
        src/ui/audio/audio.cpp
        src/ui/audio/audio-backend.h
        src/ui/audio/audio-backend-factory.cpp
        src/ui/audio/null-audio-backend.cpp
        src/ui/audio/null-audio-backend.h
    )
    message(STATUS "QSanguosha audio backend: ${QSAN_AUDIO_BACKEND}")

    if(QSAN_AUDIO_BACKEND STREQUAL "FMOD")
        # bundled fmodex.lib 只喺 Release 連結，所以 FMOD 實作亦只喺 Release
        # 編入；Debug 落返 null backend，同以前「Debug 冇 AUDIO_SUPPORT」一樣冇聲。
        target_sources(QSanguosha PRIVATE
            "$<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/src/ui/audio/fmod-audio-backend.cpp>"
            src/ui/audio/fmod-audio-backend.h
        )
        target_compile_definitions(QSanguosha PRIVATE
            "$<$<CONFIG:Release>:QSAN_AUDIO_BACKEND_FMOD>"
        )
    elseif(QSAN_AUDIO_BACKEND STREQUAL "QT")
        target_sources(QSanguosha PRIVATE
            src/ui/audio/qt-audio-backend.cpp
            src/ui/audio/qt-audio-backend.h
        )
        target_compile_definitions(QSanguosha PRIVATE QSAN_AUDIO_BACKEND_QT)
    endif()

    target_include_directories(QSanguosha PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include/spine-cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dialog
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/audio
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/home
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )

    if(WIN32)
        # bundled Freetype／FMOD／vld header 只供 Windows 使用；放喺 Linux include
        # path 會遮蔽系統 Freetype header（ft2build.h 與 <freetype/*.h> 混版）。
        target_include_directories(QSanguosha PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
            "$<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/include/fmod>"
        )
    endif()

    # Qt QML import path / bin 目錄：Windows 的 Qt prefix layout 與 Linux distro
    # layout 不同，改由 Qt 自身回報的安裝路徑推導。
    if(DEFINED QT6_INSTALL_PREFIX AND DEFINED QT6_INSTALL_QML)
        if(IS_ABSOLUTE "${QT6_INSTALL_QML}")
            set(QSAN_QT_QML_DIR "${QT6_INSTALL_QML}")
        else()
            set(QSAN_QT_QML_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
        endif()
    else()
        set(QSAN_QT_QML_DIR "${QT_PREFIX}/qml")
    endif()
    if(DEFINED QT6_INSTALL_PREFIX AND DEFINED QT6_INSTALL_BINS)
        if(IS_ABSOLUTE "${QT6_INSTALL_BINS}")
            set(QSAN_QT_BIN_DIR "${QT6_INSTALL_BINS}")
        else()
            set(QSAN_QT_BIN_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
        endif()
    else()
        set(QSAN_QT_BIN_DIR "${QT_PREFIX}/bin")
    endif()

    target_compile_definitions(QSanguosha PRIVATE
        USING_PCH
        QT_DEPRECATED_WARNINGS
        QT_DISABLE_DEPRECATED_BEFORE=0x060000
        QT_QML_IMPORT_PATH="${QSAN_QT_QML_DIR}"
        QT_BIN_DIR="${QSAN_QT_BIN_DIR}"
        HAS_QT_MULTIMEDIA
        # facade 喺所有 GUI config 都存在（最差情況係 null backend），所以
        # AUDIO_SUPPORT 而家係「呢個 target 有 audio facade」而唔再係
        # 「呢個 target 連得到 FMOD」。engine／server target 一樣冇呢個定義，
        # 佢哋照舊唔會拉到任何 audio 實作。
        AUDIO_SUPPORT
        QSAN_AUDIO_BACKEND_NAME="${QSAN_AUDIO_BACKEND}"
    )

    if(WIN32)
        target_compile_definitions(QSanguosha PRIVATE
            WIN64
            _CRT_SECURE_NO_WARNINGS
        )
    endif()

    if(MSVC)
        target_compile_options(QSanguosha PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
            "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/bigobj;/MP8>"
        )
    endif()

    target_precompile_headers(QSanguosha PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h>"
    )

    target_link_libraries(QSanguosha PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
        Qt6::Core
        Qt6::Core5Compat
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Network
        Qt6::OpenGLWidgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickWidgets
        Qt6::Widgets
    )

    if(WIN32)
        set(qsan_library_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x64")
        foreach(required_library freetype.lib freetype_D.lib fmodex.lib)
            if(NOT EXISTS "${qsan_library_dir}/${required_library}")
                message(FATAL_ERROR "Missing bundled library: ${qsan_library_dir}/${required_library}")
            endif()
        endforeach()

        target_link_libraries(QSanguosha PRIVATE
            dwmapi
            legacy_stdio_definitions
            winhttp
            "$<$<CONFIG:Release>:dbghelp>"
            "$<$<CONFIG:Release>:user32>"
            "$<$<CONFIG:Release>:gdi32>"
            "$<$<CONFIG:Debug>:${qsan_library_dir}/freetype_D.lib>"
            "$<$<NOT:$<CONFIG:Debug>>:${qsan_library_dir}/freetype.lib>"
            "$<$<CONFIG:Release>:${qsan_library_dir}/fmodex.lib>"
        )
    else()
        # Linux 使用系統 Freetype；不 link 任何 bundled *.lib，亦不需要 FMOD。
        find_package(Freetype REQUIRED)
        target_link_libraries(QSanguosha PRIVATE Freetype::Freetype)
    endif()

    if(MSVC)
        target_link_options(QSanguosha PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    endif()

    set_target_properties(QSanguosha PROPERTIES
        QT_QML_MODULE_NO_IMPORT_SCAN TRUE
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/relwithdebinfo"
        PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/debug"
        PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/release"
        PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/relwithdebinfo"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    set(translation_source "${CMAKE_CURRENT_SOURCE_DIR}/builds/sanguosha.ts")
    set_source_files_properties("${translation_source}" PROPERTIES
        OUTPUT_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    qt_add_lrelease(QSanguosha
        TS_FILES "${translation_source}"
        QM_FILES_OUTPUT_VARIABLE qsan_qm_files
    )

    qt_add_lupdate(QSanguosha
        TS_FILES "${translation_source}"
        SOURCES
            ${QSAN_SOURCES}
            src/core/engine.cpp
            ${QSAN_FORMS}
            qml/home/HomeScene.qml
            qml/home/HomeMainButton.qml
            qml/home/MainActionPanel.qml
            qml/home/HomeBottomBar.qml
            qml/home/HomeNavButton.qml
            qml/home/HomeSideBar.qml
            qml/home/GeneralScene.qml
            qml/home/HomeTheme.qml
    )

    if(WIN32)
        set(QSAN_FMOD_RUNTIME "" CACHE FILEPATH "Path to fmodex64.dll for Release deployment")
        find_program(QSAN_WINDEPLOYQT
            NAMES windeployqt
            HINTS "${QT6_INSTALL_PREFIX}/bin"
            NO_DEFAULT_PATH
            REQUIRED
        )
        add_custom_target(deploy
            COMMAND ${CMAKE_COMMAND}
                -DQSAN_CONFIG=$<CONFIG>
                -DQSAN_EXECUTABLE=$<TARGET_FILE:QSanguosha>
                -DQSAN_WINDEPLOYQT=${QSAN_WINDEPLOYQT}
                -DQSAN_QML_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DQSAN_FMOD_RUNTIME=${QSAN_FMOD_RUNTIME}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Deploy.cmake
            DEPENDS QSanguosha
            VERBATIM
        )
    endif()
endif()

if(WIN32)
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        ${QSAN_SOURCES}
        ${QSAN_LUA_SOURCES}
        ${QSAN_MOC_HEADERS}
        ${QSAN_FORMS}
    )
endif()
source_group("Generated" FILES "${qsan_swig_wrapper}")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
