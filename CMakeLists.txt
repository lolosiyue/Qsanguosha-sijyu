cmake_minimum_required(VERSION 3.28)

project(QSanguosha LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)

# Windows XP is an opt-in legacy product built from the same source tree.  It
# deliberately keeps Qt 5.6/v141_xp out of the normal Qt 6 build graph.
option(QSAN_BUILD_XP_LEGACY
    "Build the Windows XP SP3 x86 legacy client/server executable"
    OFF
)

if(QSAN_BUILD_XP_LEGACY AND NOT WIN32)
    message(FATAL_ERROR "QSAN_BUILD_XP_LEGACY is available on Windows only")
endif()

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
if(QSAN_BUILD_XP_LEGACY)
    set(QSAN_BUILD_SERVER_DEFAULT OFF)
else()
    set(QSAN_BUILD_SERVER_DEFAULT ON)
endif()
option(QSAN_BUILD_SERVER
    "Build the dedicated QSanguosha server"
    ${QSAN_BUILD_SERVER_DEFAULT}
)
option(QSAN_BUILD_TUI
    "Build the Protocol V2 terminal client"
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
    if(QSAN_BUILD_XP_LEGACY)
        if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
            message(FATAL_ERROR "The Windows XP legacy build requires Win32 MSVC")
        endif()
        if(NOT CMAKE_VS_PLATFORM_TOOLSET STREQUAL "v141_xp")
            message(FATAL_ERROR
                "The Windows XP legacy build requires the v141_xp toolset (got '${CMAKE_VS_PLATFORM_TOOLSET}')")
        endif()
    else()
        if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            message(FATAL_ERROR "The Windows CMake build supports x64 MSVC only")
        endif()
        if(MSVC_VERSION LESS 1950)
            message(FATAL_ERROR "The Windows CMake build supports the Visual Studio 2026 v145 toolchain only")
        endif()
    endif()
elseif(NOT UNIX)
    message(FATAL_ERROR "Unsupported platform")
endif()

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(QSAN_BUILD_XP_LEGACY)
    # MSVC enables thread-safe local static initialization by default, but its
    # runtime fast path depends on Vista-era TLS support. Qt 5.6's
    # QStringLiteral uses function-local statics heavily, so keep the complete
    # XP target graph on the pre-VS2015 initialization model.
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/Zc:threadSafeInit->")
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# WebSockets 閘道用 QWebSocket::setMaxAllowedIncomingFrameSize（Qt 5.15+）
# 與 QOverload（Qt 5.7+）。Qt 5.6.3／XP 編不過，原生 TCP 仍是該產品的 listen 路徑。
set(QSAN_ENABLE_WEBSOCKETS ON)
if(QSAN_BUILD_XP_LEGACY)
    set(QSAN_ENABLE_WEBSOCKETS OFF)
endif()

# 單一 Qt component 清單，按產品分層，避免 Windows／Linux 各自維護會漂移的兩份清單。
set(QSAN_QT_COMPONENTS Core Network)
if(QSAN_ENABLE_WEBSOCKETS)
    list(APPEND QSAN_QT_COMPONENTS WebSockets)
endif()

if(BUILD_TESTING)
    list(APPEND QSAN_QT_COMPONENTS
        Gui
    )
endif()

if(QSAN_BUILD_GUI)
    if(QSAN_BUILD_XP_LEGACY)
        list(APPEND QSAN_QT_COMPONENTS Gui Widgets)
    else()
        list(APPEND QSAN_QT_COMPONENTS
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
endif()

list(REMOVE_DUPLICATES QSAN_QT_COMPONENTS)

# GUI 產品的 Qt baseline 在 Windows 與 Linux 保持一致。GUI source 用到
# Qt 6.5 才加入的 API（例如 QStyleHints::colorScheme()），且 Windows 正式
# toolchain 已經係 Qt 6.11.1，所以 GUI build 明確要求 6.11。
#
# server-only build 刻意唔套用呢個下限：dedicated server 只需要
# Core／Network（Qt6 再加 WebSockets），要繼續可以用 distro 提供的 Qt 6.x
# （例如 Ubuntu 24.04 的 6.4.2）建置。XP／Qt 5.6.3 不找 WebSockets。
set(QSAN_QT_GUI_MINIMUM_VERSION 6.11)

if(QSAN_BUILD_GUI OR WIN32)
    set(qsan_qt_minimum_version ${QSAN_QT_GUI_MINIMUM_VERSION})
else()
    set(qsan_qt_minimum_version "")
endif()

if(QSAN_BUILD_XP_LEGACY)
    find_package(Qt5 5.6.3 EXACT REQUIRED COMPONENTS ${QSAN_QT_COMPONENTS})
    foreach(qsan_qt_component IN LISTS QSAN_QT_COMPONENTS)
        # The production CMake graph uses Qt6::* target names throughout.
        # Local aliases let the legacy graph reuse it without exposing Qt5 to
        # the default build or duplicating the engine source inventory.
        add_library("Qt6::${qsan_qt_component}" ALIAS "Qt5::${qsan_qt_component}")
    endforeach()
    get_filename_component(QT_PREFIX "${Qt5_DIR}/../../.." ABSOLUTE)
    message(STATUS "QSanguosha XP legacy Qt baseline: 5.6.3 (found ${Qt5_VERSION})")
else()
    find_package(Qt6 ${qsan_qt_minimum_version} REQUIRED COMPONENTS ${QSAN_QT_COMPONENTS})

    if(QSAN_BUILD_GUI)
        message(STATUS "QSanguosha GUI Qt baseline: ${QSAN_QT_GUI_MINIMUM_VERSION} (found ${Qt6_VERSION})")
    endif()

    get_filename_component(QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
endif()

set(QSAN_ENGINE_QT_LIBS Qt6::Core Qt6::Network)
if(QSAN_ENABLE_WEBSOCKETS)
    list(APPEND QSAN_ENGINE_QT_LIBS Qt6::WebSockets)
endif()

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
set(qsan_swig_options)
if(QSAN_BUILD_XP_LEGACY)
    list(APPEND qsan_swig_options -DQSAN_XP_LEGACY)
endif()
add_custom_command(
    OUTPUT "${qsan_swig_wrapper}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${qsan_generated_dir}"
    COMMAND "${QSAN_SWIG_EXECUTABLE}" -c++ -lua ${qsan_swig_options}
        -o "${qsan_swig_wrapper}"
        "${CMAKE_CURRENT_SOURCE_DIR}/swig/sanguosha.i"
    DEPENDS ${qsan_swig_interfaces}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating C++/Lua bindings with SWIG"
    VERBATIM
)
set_source_files_properties("${qsan_swig_wrapper}" PROPERTIES GENERATED TRUE)

include(cmake/QSanguoshaSources.cmake)

if(QSAN_BUILD_XP_LEGACY)
    # XP keeps the current rules/client/server sources, but never compiles the
    # Qt Quick, Spine, video or OpenGL implementation units.
    list(FILTER QSAN_SOURCES EXCLUDE REGEX "^src/spine/")
    list(FILTER QSAN_SOURCES EXCLUDE REGEX "^src/ui/home/")
    list(REMOVE_ITEM QSAN_SOURCES
        src/main.cpp
        src/ui/CharacterSpineActionController.cpp
        src/ui/EmbeddedQmlLoader.cpp
        src/ui/pointer-effect-overlay.cpp
        src/ui/SpineAnimationManager.cpp
        src/ui/SpineEffectWidget.cpp
        src/ui/SpineGlItem.cpp
        src/ui/SpineIndicatorLine.cpp
    )
    list(PREPEND QSAN_SOURCES legacy/xp/src/xp-main.cpp)

    list(FILTER QSAN_MOC_HEADERS EXCLUDE REGEX "^src/ui/home/")
    list(REMOVE_ITEM QSAN_MOC_HEADERS
        src/ui/CharacterSpineActionController.h
        src/ui/EmbeddedQmlLoader.h
        src/ui/pointer-effect-overlay.h
        src/ui/SpineAnimationManager.h
        src/ui/SpineEffectWidget.h
        src/ui/SpineGlItem.h
        src/ui/SpineIndicatorLine.h
    )
endif()

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
    src/core/asset-manifest.cpp
    src/core/asset-manifest.h
    src/core/runtime-paths.cpp
    src/core/runtime-paths.h
    src/core/engine.cpp
    src/core/engine.h
    src/core/general-version.cpp
    src/core/general-version.h
    src/core/package-selection-policy.cpp
    src/core/package-selection-policy.h
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
    src/core/protocol/protocol-codec.h
    src/core/protocol/card-provenance-message.cpp
    src/core/protocol/card-provenance-message.h
    src/core/protocol/gameplay/multiple-choice-payload.cpp
    src/core/protocol/gameplay/multiple-choice-payload.h
    src/core/protocol/gameplay/protocol-gameplay-payload-registry.cpp
    src/core/protocol/gameplay/protocol-gameplay-payload-registry.h
    src/core/protocol/gameplay/simple-choice-payloads.cpp
    src/core/protocol/gameplay/simple-choice-payloads.h
    src/core/protocol/gameplay/typed-interaction-payloads.cpp
    src/core/protocol/gameplay/typed-interaction-payloads.h
    src/core/protocol/protocol-message.cpp
    src/core/protocol/protocol-message.h
    src/core/protocol/protocol-message-utils.h
    src/core/protocol/protocol-payload-registry.cpp
    src/core/protocol/protocol-payload-registry.h
    src/core/protocol/session/session-payloads.cpp
    src/core/protocol/session/session-payloads.h
    src/core/protocol/session/client-session-controller.cpp
    src/core/protocol/session/client-session-controller.h
    src/core/protocol/protocol-runtime.cpp
    src/core/protocol/protocol-runtime.h
    src/core/protocol/protocol-v2-codec.cpp
    src/core/protocol/protocol-v2-codec.h
    src/core/protocol/protocol-version.h
    src/core/replay/replay-codec.cpp
    src/core/replay/replay-codec.h
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
    src/server/server-core.h
    src/server/server-connection-context.cpp
    src/server/server-connection-context.h
    src/server/ai.cpp
    src/server/ai-decision-coordinator.cpp
    src/server/ai-decision-coordinator.h
    src/server/ai-runtime.cpp
    src/server/ai-runtime.h
    src/server/basicai.cpp
    src/server/card-movement-service.cpp
    src/server/card-movement-service.h
    src/server/card-state-service.cpp
    src/server/card-state-service.h
    src/server/event-dispatcher.h
    src/server/extra-turn-scheduler.cpp
    src/server/extra-turn-scheduler.h
    src/server/game-session-controller.cpp
    src/server/game-session-controller.h
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
    src/server/player-state-service.cpp
    src/server/player-state-service.h
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
    src/client/client-log-formatter.cpp
    src/client/client-log-formatter.h
    ${QSAN_ENGINE_PACKAGE_SOURCES}
    ${QSAN_LUA_SOURCES}
    "${qsan_swig_wrapper}"
)

set_source_files_properties(${QSAN_LUA_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
list(REMOVE_ITEM QSAN_SOURCES src/util/game-snapshot.cpp)
list(REMOVE_ITEM QSAN_SOURCES src/client/aux-skills.cpp)
list(REMOVE_ITEM QSAN_MOC_HEADERS src/util/game-snapshot.h src/util/socket.h src/client/aux-skills.h)

target_include_directories(qsanguosha_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    # package／scenario 嘅 TU include 到 client.h,而 client.h 而家見到
    # ClientCore。呢個係 header-only 依賴:engine 唔會 link
    # qsanguosha_client_core,亦冇 call 過佢任何 symbol。
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/package
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
)

if(QSAN_BUILD_XP_LEGACY)
    target_include_directories(qsanguosha_engine BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5
    )
endif()

target_compile_definitions(qsanguosha_engine PRIVATE
    QSAN_ENGINE_BUILD
    QSAN_SERVER_CORE_ONLY
    "$<$<CONFIG:Release>:QSG_CRASH_HANDLER>"
)

if(QSAN_BUILD_XP_LEGACY)
    target_compile_definitions(qsanguosha_engine PRIVATE
        QSAN_XP_LEGACY
        QSAN_ENABLE_QML=0
        QSAN_ENABLE_SPINE=0
        QSAN_ENABLE_VIDEO=0
        QSAN_ENABLE_WEBSOCKETS=0
        QSAN_USE_RASTER_VIEWPORT=1
        WIN32
        WINVER=0x0501
        _WIN32_WINNT=0x0501
        _WIN32_IE=0x0600
    )
endif()

if(MSVC)
    if(QSAN_BUILD_XP_LEGACY)
        target_compile_definitions(qsanguosha_engine PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(qsanguosha_engine PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5/qsan-qt5-compat.h")
    else()
        target_compile_definitions(qsanguosha_engine PRIVATE WIN64 _CRT_SECURE_NO_WARNINGS)
    endif()
    target_compile_options(qsanguosha_engine PRIVATE /utf-8 /bigobj /MP8)
endif()

target_precompile_headers(qsanguosha_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine-pch.h
)

if(QSAN_ENABLE_WEBSOCKETS)
    target_sources(qsanguosha_engine PRIVATE
        src/util/websocketsocket.cpp
        src/util/websocketsocket.h
    )
endif()

target_link_libraries(qsanguosha_engine PUBLIC ${QSAN_ENGINE_QT_LIBS})

if(WIN32)
    target_link_libraries(qsanguosha_engine PRIVATE Ws2_32 IPHLPAPI)
endif()

get_target_property(qsan_engine_links qsanguosha_engine LINK_LIBRARIES)
set(_qsan_engine_qt_allowlist ${QSAN_ENGINE_QT_LIBS})
foreach(qsan_engine_link IN LISTS qsan_engine_links)
    if(qsan_engine_link MATCHES "^Qt6::" AND NOT qsan_engine_link IN_LIST _qsan_engine_qt_allowlist)
        message(FATAL_ERROR "qsanguosha_engine links forbidden Qt target: ${qsan_engine_link}")
    endif()
endforeach()

# Client Architecture F1:ClientCore。
#
# 中間層自己一個 static library,而唔係 GUI target 入面嘅幾個 .cpp,係為咗令
# 「ClientCore 唔依賴 GUI」由 build system 保證,而唔係靠人自律:下面嘅
# allowlist gate 只准 Qt6::Core,所以任何人試圖喺 ClientCore 度 include
# QWidget／QQuickItem／Dashboard／RoomScene 都會喺 configure 或者 link 爆。
#
# 佢亦刻意唔 link qsanguosha_engine:interaction model 只講卡 id、玩家
# objectName 同 option 字串,規則真相仍然喺 server。dedicated server build
# 一樣會起呢個 target,所以 server-only configure 都驗得到佢冇 GUI 依賴。
set(QSAN_CLIENT_CORE_SOURCES
    src/client/core/client-core.cpp
    src/client/core/client-core.h
    src/client/core/card-eligibility-provider.h
    src/client/core/client-game-state.cpp
    src/client/core/client-game-state.h
    src/client/core/client-game-state-reducer.cpp
    src/client/core/client-game-state-reducer.h
    src/client/core/client-interaction-view.h
    src/client/core/client-interaction-presenter.h
    src/client/core/interaction-model.cpp
    src/client/core/interaction-model.h
    src/client/core/custom-interaction-registry.h
    src/client/core/client-move-log.cpp
    src/client/core/client-move-log.h
)

add_library(qsanguosha_client_core STATIC ${QSAN_CLIENT_CORE_SOURCES})
set_target_properties(qsanguosha_client_core PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(qsanguosha_client_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)
if(QSAN_BUILD_XP_LEGACY)
    target_include_directories(qsanguosha_client_core BEFORE PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5
    )
    target_compile_definitions(qsanguosha_client_core PRIVATE
        QSAN_XP_LEGACY
        WIN32
        WINVER=0x0501
        _WIN32_WINNT=0x0501
    )
endif()
target_link_libraries(qsanguosha_client_core PUBLIC Qt6::Core)
if(MSVC)
    target_compile_definitions(qsanguosha_client_core PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(qsanguosha_client_core PRIVATE /utf-8)
    if(QSAN_BUILD_XP_LEGACY)
        target_compile_options(qsanguosha_client_core PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5/qsan-qt5-compat.h")
    endif()
endif()

get_target_property(qsan_client_core_links qsanguosha_client_core LINK_LIBRARIES)
foreach(qsan_client_core_link IN LISTS qsan_client_core_links)
    if(NOT qsan_client_core_link STREQUAL "Qt6::Core")
        message(FATAL_ERROR
            "qsanguosha_client_core links forbidden target: ${qsan_client_core_link}; only Qt6::Core is allowed")
    endif()
endforeach()

if(QSAN_BUILD_TUI)
    add_library(qsanguosha_tui_support STATIC
        src/client/client-live-session.cpp
        src/client/client-live-session.h
        src/client/interaction-command-registry.cpp
        src/client/interaction-command-registry.h
        src/client/interaction-reply-coordinator.cpp
        src/client/interaction-reply-coordinator.h
        src/client/interaction-reply-encoder.cpp
        src/client/interaction-reply-encoder.h
        src/client/interaction-request-factory.cpp
        src/client/interaction-request-factory.h
        src/client/protocol-interaction-request-builder.cpp
        src/client/protocol-interaction-request-builder.h
        src/tui/tui-application-controller.cpp
        src/tui/tui-application-controller.h
        src/tui/tui-card-text.cpp
        src/tui/tui-card-text.h
        src/tui/tui-command.cpp
        src/tui/tui-command.h
        src/tui/tui-log-text.cpp
        src/tui/tui-log-text.h
        src/tui/tui-input.cpp
        src/tui/tui-input.h
        src/tui/tui-interaction-view.cpp
        src/tui/tui-interaction-view.h
        src/tui/tui-renderer.cpp
        src/tui/tui-renderer.h
        src/tui/tui-script-runner.cpp
        src/tui/tui-script-runner.h
    )
    target_include_directories(qsanguosha_tui_support PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )
    target_compile_definitions(qsanguosha_tui_support PRIVATE QSAN_ENGINE_TEST_BUILD)
    target_link_libraries(qsanguosha_tui_support PUBLIC
        qsanguosha_client_core
        Qt6::Core
        Qt6::Network
    )
    target_link_libraries(qsanguosha_tui_support PRIVATE
        qsanguosha_engine
    )
    # Static libraries can hide playback code from dumpbin/ldd. Reject every
    # Replay source from the TUI frontend and Recorder/Replayer playback sources
    # from its shared engine. The server-side Protocol V2 ReplayCodec remains a
    # shared protocol implementation but has no TUI entry point or dependency.
    foreach(qsan_tui_audit_target qsanguosha_engine qsanguosha_tui_support)
        get_target_property(qsan_tui_audit_sources ${qsan_tui_audit_target} SOURCES)
        foreach(qsan_tui_audit_source IN LISTS qsan_tui_audit_sources)
            string(TOLOWER "${qsan_tui_audit_source}" qsan_tui_audit_source_lower)
            set(qsan_tui_forbidden_source FALSE)
            if(qsan_tui_audit_target STREQUAL "qsanguosha_tui_support"
                    AND qsan_tui_audit_source_lower MATCHES "(^|/)replay[^/]*\\.")
                set(qsan_tui_forbidden_source TRUE)
            elseif(qsan_tui_audit_source_lower MATCHES
                    "(^|/)(recorder|replayer)[^/]*\\.(c|cc|cpp|cxx|h|hpp)$")
                set(qsan_tui_forbidden_source TRUE)
            endif()
            if(qsan_tui_forbidden_source)
                message(FATAL_ERROR
                    "${qsan_tui_audit_target} contains forbidden Replay playback source: ${qsan_tui_audit_source}")
            endif()
        endforeach()
    endforeach()
    if(MSVC)
        target_compile_definitions(qsanguosha_tui_support PRIVATE
            WIN64 _CRT_SECURE_NO_WARNINGS)
        target_compile_options(qsanguosha_tui_support PRIVATE /utf-8 /bigobj /MP8)
    endif()

    add_executable(qsanguosha_tui src/tui/tui-main.cpp)
    target_include_directories(qsanguosha_tui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tui
    )
    target_compile_definitions(qsanguosha_tui PRIVATE QSAN_ENGINE_TEST_BUILD)
    target_link_libraries(qsanguosha_tui PRIVATE
        qsanguosha_tui_support
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
        ${QSAN_ENGINE_QT_LIBS}
    )
    if(WIN32)
        # crash handler (crashhandler.cpp, Release only) needs dbghelp
        target_link_libraries(qsanguosha_tui PRIVATE
            "$<$<CONFIG:Release>:dbghelp>"
            "$<$<CONFIG:Release>:user32>"
            "$<$<CONFIG:Release>:gdi32>"
        )
    endif()
    if(MSVC)
        target_compile_definitions(qsanguosha_tui PRIVATE WIN64 _CRT_SECURE_NO_WARNINGS)
        target_compile_options(qsanguosha_tui PRIVATE /utf-8 /bigobj /MP8)
    endif()
    set_target_properties(qsanguosha_tui PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/relwithdebinfo"
        PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/debug"
        PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/release"
        PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/relwithdebinfo"
    )

    if(WIN32)
        get_filename_component(qsan_linker_directory "${CMAKE_LINKER}" DIRECTORY)
        find_program(QSAN_DUMPBIN_EXECUTABLE
            NAMES dumpbin.exe
            HINTS
                "${qsan_linker_directory}"
                "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
            REQUIRED
        )
    endif()
    add_custom_command(TARGET qsanguosha_tui POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DQSAN_TUI_EXECUTABLE=$<TARGET_FILE:qsanguosha_tui>"
            "-DQSAN_DUMPBIN=${QSAN_DUMPBIN_EXECUTABLE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyTuiDependencies.cmake"
        VERBATIM
    )

    if(WIN32)
        find_program(QSAN_TUI_WINDEPLOYQT
            NAMES windeployqt
            HINTS "${QT6_INSTALL_PREFIX}/bin"
            NO_DEFAULT_PATH
            REQUIRED
        )
        add_custom_target(deploy-tui
            COMMAND ${CMAKE_COMMAND}
                "-DQSAN_CONFIG=$<CONFIG>"
                "-DQSAN_EXECUTABLE=$<TARGET_FILE:qsanguosha_tui>"
                "-DQSAN_WINDEPLOYQT=${QSAN_TUI_WINDEPLOYQT}"
                "-DQSAN_DUMPBIN=${QSAN_DUMPBIN_EXECUTABLE}"
                "-DQSAN_VS_INSTALL_DIR=${CMAKE_GENERATOR_INSTANCE}"
                "-DQSAN_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/dist/tui/$<CONFIG>"
                "-DQSAN_ASSET_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployTui.cmake"
            DEPENDS qsanguosha_tui
            VERBATIM
        )
    endif()
endif()

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
        src/server/server-status.h
    )
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
        ${QSAN_ENGINE_QT_LIBS}
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

# ---------------------------------------------------------------------------
# Linux install layout (M3)
#
# 三個 install component,令「GUI 包」同「server 包」可以各自出貨:
#
#   qsan_data    lua / extensions / lang / etc —— 兩邊都要的規則資料
#   qsan_server  dedicated server binary、systemd unit、server 文件
#   qsan_gui     GUI binary、介面資產、desktop entry、圖示
#
# server-only 安裝因此唔會拉到任何 GUI 資產,亦唔會拉到 Qt GUI runtime。
# ---------------------------------------------------------------------------
if(UNIX)
    set(QSAN_DATA_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/qsanguosha")

    # bin/ 望去 share/qsanguosha/ 的相對路徑。runtime resolver 用呢個而唔用
    # 絕對的 CMAKE_INSTALL_FULL_DATADIR:binary 入面唔應該燒死安裝機器的路徑,
    # 而且 portable bundle 同 AppImage 要可以解壓到任何地方都行得到。
    file(RELATIVE_PATH QSAN_BIN_TO_DATA_RELATIVE
        "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_DATADIR}/qsanguosha"
    )
    # bin/ 望去私有 Qt runtime 的相對路徑。Qt 自己的 library／plugin／QML
    # plugin 都已經帶 $ORIGIN-relative RUNPATH,而且假設標準 prefix 版面
    # (lib/、plugins/<type>/、qml/<Module>/ 全部 $ORIGIN/../../lib),所以
    # 私有 runtime 必須照抄呢個版面 —— 咁就唔需要 patchelf 改寫每個 Qt .so。
    set(QSAN_PRIVATE_QT_DIR "${CMAKE_INSTALL_LIBDIR}/qsanguosha/qt")
    file(RELATIVE_PATH QSAN_BIN_TO_QT_LIB_RELATIVE
        "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_LIBDIR}/qsanguosha/qt/lib"
    )

    target_compile_definitions(qsanguosha_engine PRIVATE
        QSAN_BIN_TO_DATA_RELATIVE="${QSAN_BIN_TO_DATA_RELATIVE}"
    )

    # 資產清單:安裝樹入面實際有乜嘢係「冇咗行唔到」、乜嘢係「冇咗只係少啲
    # 聲畫」。GUI 專用資產只有喺 GUI build 先至列入 required。
    # 唔係每個 build context 都有齊呢啲目錄:Docker server image 嘅
    # .dockerignore 就特登剔走 GUI/client 資產(lang/、qss/、skins/、
    # ui-script/),而 extensions/ 同 lua/ai/ 係 build 入面 fetch 返嚟。
    # install(DIRECTORY) 撞到唔存在嘅目錄會直接 fatal,所以逐個檢查存在先裝;
    # 「應該有但係冇」由資產清單負責大聲講出嚟,而唔係喺 install 中途炸。
    set(QSAN_DATA_DIRECTORIES lua)
    foreach(qsan_data_directory lang etc extensions)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${qsan_data_directory}")
            list(APPEND QSAN_DATA_DIRECTORIES ${qsan_data_directory})
        endif()
    endforeach()

    set(QSAN_ASSET_REQUIRED
        lua/config.lua
        lua/sanguosha.lua
        lua/sgs_ex.lua
        lua/utilities.lua
        lua/chat_config.lua
        lua/lib
        lua/ai/smart-ai.lua
        extensions
    )
    set(QSAN_ASSET_OPTIONAL
        etc/customScenes
        image
        audio
        font
        hero-skin
    )
    # lang/ 冇咗唔會 crash —— sgs.GetFileNames() 對住一個唔存在嘅目錄回空
    # list,遊戲照跑,只係顯示內部名而唔係中文。dedicated server 一路以嚟
    # (Docker image)就係咁行。但一個冇翻譯嘅 GUI 對玩家嚟講唔算能用,
    # 所以 GUI build 當佢係 required。
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lang")
        if(QSAN_BUILD_GUI)
            list(APPEND QSAN_ASSET_REQUIRED lang/zh_CN)
        else()
            list(APPEND QSAN_ASSET_OPTIONAL lang/zh_CN)
        endif()
    endif()
    if(QSAN_BUILD_GUI)
        list(APPEND QSAN_ASSET_REQUIRED
            skins/skinList.json
            skins/resource_aliases.json
            qss
            ui-script/animation.qml
        )
        list(APPEND QSAN_ASSET_OPTIONAL
            translations/sanguosha.qm
            translations/qt_zh_CN.qm
            # 首頁 QML 圖示同背景:唔入庫,clean checkout 開 GUI 一定會見到
            # 一批 "QML Image: Cannot open" warning。列喺度令嗰批 warning
            # 由「唔知係咩」變成「manifest 講明預期缺失」。
            qml/home/icons
            image/system/backdrop
        )
    endif()

    # 版本欄位:game_version 跟 src/core/version.h,asset_pack_version 描述
    # 呢個 core package 帶咗邊一套資產(唔包大型美術／音訊)。
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/version.h" qsan_version_line
        REGEX "Number\\[\\] *= *\"")
    string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" QSAN_GAME_VERSION "${qsan_version_line}")
    if(NOT QSAN_GAME_VERSION)
        message(FATAL_ERROR "Unable to read QSanVersion::Number from src/core/version.h")
    endif()
    set(QSAN_ASSET_PACK_VERSION "core-1")

    string(REPLACE ";" "\",\n    \"" QSAN_ASSET_REQUIRED_JSON "${QSAN_ASSET_REQUIRED}")
    set(QSAN_ASSET_REQUIRED_JSON "\"${QSAN_ASSET_REQUIRED_JSON}\"")
    string(REPLACE ";" "\",\n    \"" QSAN_ASSET_OPTIONAL_JSON "${QSAN_ASSET_OPTIONAL}")
    set(QSAN_ASSET_OPTIONAL_JSON "\"${QSAN_ASSET_OPTIONAL_JSON}\"")

    set(QSAN_ASSET_MANIFEST_FILE "${CMAKE_CURRENT_BINARY_DIR}/assets-manifest.json")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/assets-manifest.json.in"
        "${QSAN_ASSET_MANIFEST_FILE}"
        @ONLY
    )
    # 刻意唔喺 source tree 放一份:GUI build 同 server-only build 的 required
    # 清單唔同,兩個 build directory 會互相覆寫同一個檔案,令開發樹嘅
    # --asset-report 講緊邊個 build 完全睇彩數。開發樹冇 manifest 係正常情況,
    # QSanAssetManifest::inspect() 會老實報「無法驗證」而唔係報錯。

    install(DIRECTORY ${QSAN_DATA_DIRECTORIES}
        DESTINATION "${QSAN_DATA_INSTALL_DIR}"
        COMPONENT qsan_data
        PATTERN "data/AiData*" EXCLUDE
        PATTERN "*.log" EXCLUDE
    )
    install(FILES "${QSAN_ASSET_MANIFEST_FILE}"
        DESTINATION "${QSAN_DATA_INSTALL_DIR}"
        COMPONENT qsan_data
    )
    install(FILES gpl-3.0.txt LICENSE
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        COMPONENT qsan_data
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
    # 私有 Qt runtime 喺 distro 式安裝根本唔存在,個時 loader 直接落系統 Qt;
    # 喺 portable／AppImage 就會命中 bundle 入面嗰份。同一個 binary 兩邊都啱。
    set_target_properties(qsanguosha_server PROPERTIES
        INSTALL_RPATH "$ORIGIN/${QSAN_BIN_TO_QT_LIB_RELATIVE}"
    )
    install(TARGETS qsanguosha_server
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT qsan_server
    )
    if(QSAN_BUILD_TUI)
        set_target_properties(qsanguosha_tui PROPERTIES
            INSTALL_RPATH "$ORIGIN/${QSAN_BIN_TO_QT_LIB_RELATIVE}"
        )
        install(TARGETS qsanguosha_tui
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT qsan_tui
        )
        install(FILES docs/tui-client.md
            DESTINATION "${CMAKE_INSTALL_DOCDIR}"
            COMPONENT qsan_tui
        )
    endif()
    install(FILES docs/server.ini.example
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        COMPONENT qsan_server
    )
    install(FILES docs/linux-development-environment.md
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        COMPONENT qsan_server
    )
    install(FILES "${QSAN_SYSTEMD_SERVICE_FILE}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/system"
        COMPONENT qsan_server
    )
endif()


if(QSAN_BUILD_GUI)
    # GUI 產品的 source／MOC header／form／QML resource／askFor runner 由 Windows
    # 與 Linux 共用；平台只追加平台專屬項目，不維護第二套 source list。
    set(QSAN_GUI_SOURCES
        ${QSAN_SOURCES}
        ${QSAN_MOC_HEADERS}
        ${QSAN_FORMS}
        src/core/replay/replay-container.cpp
        src/core/replay/replay-container.h
        src/ui/package-dialogs.cpp
        src/ui/package-dialogs.h
    )

    if(QSAN_BUILD_XP_LEGACY)
        set(QSAN_GUI_TARGET QSanguoshaXP)
        add_executable(${QSAN_GUI_TARGET} WIN32
            ${QSAN_GUI_SOURCES}
        )
    elseif(WIN32)
        set(QSAN_GUI_TARGET QSanguosha)
        list(APPEND QSAN_GUI_SOURCES
            resource/icon.rc
        )
        qt_add_executable(${QSAN_GUI_TARGET} WIN32
            ${QSAN_GUI_SOURCES}
        )
    else()
        set(QSAN_GUI_TARGET QSanguosha)
        qt_add_executable(${QSAN_GUI_TARGET}
            ${QSAN_GUI_SOURCES}
        )
    endif()

    if(NOT QSAN_BUILD_XP_LEGACY)
        target_sources(${QSAN_GUI_TARGET} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/qml/home/home_qml.qrc"
        )

        # home 介面圖示為本機資源，不入庫；本機存在 icons 目錄時才編譯進 qrc，避免 CI 缺少資源導致 RCC 錯誤
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qml/home/icons")
            target_sources(${QSAN_GUI_TARGET} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/qml/home/home_assets.qrc"
            )
        endif()

        target_sources(${QSAN_GUI_TARGET} PRIVATE
            src/ui/testing/local-response-ui-case.cpp
            src/ui/testing/local-response-ui-case.h
            src/ui/testing/local-response-ui-controller.cpp
            src/ui/testing/local-response-ui-controller.h
            src/ui/testing/local-response-ui-inspector.cpp
            src/ui/testing/local-response-ui-inspector.h
            src/ui/testing/local-response-ui-probe.cpp
            src/ui/testing/local-response-ui-probe.h
            src/ui/testing/effects-smoke-controller.cpp
            src/ui/testing/effects-smoke-controller.h
            src/ui/testing/effects-smoke-report.cpp
            src/ui/testing/effects-smoke-report.h
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
    else()
        # RoomScene has a small optional automation hook in production code.
        # Keep its implementation available without pulling in the QML test controllers.
        target_sources(${QSAN_GUI_TARGET} PRIVATE
            src/ui/testing/network-ui-smoke-report.cpp
            src/ui/testing/network-ui-smoke-report.h
            src/ui/testing/network-ui-smoke-responder.cpp
            src/ui/testing/network-ui-smoke-responder.h
        )
    endif()

    # Audio facade + backend。facade（src/core/audio.h + src/ui/audio/audio.cpp）
    # 同 null backend 一定編入，所以任何 config 的 call site 都有實作可以連；
    # 真正的 FMOD／Qt 實作先至按 QSAN_AUDIO_BACKEND 加入。
    target_sources(${QSAN_GUI_TARGET} PRIVATE
        src/core/audio.h
        src/ui/audio/audio.cpp
        src/ui/audio/audio-backend.h
        src/ui/audio/audio-backend-factory.cpp
        src/ui/audio/null-audio-backend.cpp
        src/ui/audio/null-audio-backend.h
    )
    message(STATUS "QSanguosha audio backend: ${QSAN_AUDIO_BACKEND}")

    if(QSAN_AUDIO_BACKEND STREQUAL "FMOD")
        if(QSAN_BUILD_XP_LEGACY)
            target_sources(${QSAN_GUI_TARGET} PRIVATE
                src/ui/audio/fmod-audio-backend.cpp
                src/ui/audio/fmod-audio-backend.h
            )
            target_compile_definitions(${QSAN_GUI_TARGET} PRIVATE QSAN_AUDIO_BACKEND_FMOD)
        else()
            # bundled fmodex.lib 只喺 Release 連結，所以 FMOD 實作亦只喺 Release
            # 編入；Debug 落返 null backend，同以前「Debug 冇 AUDIO_SUPPORT」一樣冇聲。
            target_sources(${QSAN_GUI_TARGET} PRIVATE
                "$<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/src/ui/audio/fmod-audio-backend.cpp>"
                src/ui/audio/fmod-audio-backend.h
            )
            target_compile_definitions(${QSAN_GUI_TARGET} PRIVATE
                "$<$<CONFIG:Release>:QSAN_AUDIO_BACKEND_FMOD>"
            )
        endif()
    elseif(QSAN_AUDIO_BACKEND STREQUAL "QT")
        target_sources(${QSAN_GUI_TARGET} PRIVATE
            src/ui/audio/qt-audio-backend.cpp
            src/ui/audio/qt-audio-backend.h
        )
        target_compile_definitions(${QSAN_GUI_TARGET} PRIVATE QSAN_AUDIO_BACKEND_QT)
    endif()

    target_include_directories(${QSAN_GUI_TARGET} PRIVATE
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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/effects
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/home
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )

    if(QSAN_BUILD_XP_LEGACY)
        target_include_directories(${QSAN_GUI_TARGET} BEFORE PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5
        )
    endif()

    if(WIN32)
        # bundled Freetype／FMOD／vld header 只供 Windows 使用；放喺 Linux include
        # path 會遮蔽系統 Freetype header（ft2build.h 與 <freetype/*.h> 混版）。
        target_include_directories(${QSAN_GUI_TARGET} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
            "$<$<OR:$<BOOL:${QSAN_BUILD_XP_LEGACY}>,$<CONFIG:Release>>:${CMAKE_CURRENT_SOURCE_DIR}/include/fmod>"
        )
    endif()

    # Qt QML import path / bin 目錄：Windows 的 Qt prefix layout 與 Linux distro
    # layout 不同，改由 Qt 自身回報的安裝路徑推導。
    if(QSAN_BUILD_XP_LEGACY)
        set(QSAN_QT_QML_DIR "")
        set(QSAN_QT_BIN_DIR "${QT_PREFIX}/bin")
    elseif(DEFINED QT6_INSTALL_PREFIX AND DEFINED QT6_INSTALL_QML)
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

    target_compile_definitions(${QSAN_GUI_TARGET} PRIVATE
        USING_PCH
        QT_DEPRECATED_WARNINGS
        QT_BIN_DIR="${QSAN_QT_BIN_DIR}"
        # facade 喺所有 GUI config 都存在（最差情況係 null backend），所以
        # AUDIO_SUPPORT 而家係「呢個 target 有 audio facade」而唔再係
        # 「呢個 target 連得到 FMOD」。engine／server target 一樣冇呢個定義，
        # 佢哋照舊唔會拉到任何 audio 實作。
        AUDIO_SUPPORT
        QSAN_AUDIO_BACKEND_NAME="${QSAN_AUDIO_BACKEND}"
    )

    if(QSAN_BUILD_XP_LEGACY)
        target_compile_definitions(${QSAN_GUI_TARGET} PRIVATE
            QSAN_XP_LEGACY
            QSAN_ENABLE_QML=0
            QSAN_ENABLE_SPINE=0
            QSAN_ENABLE_VIDEO=0
            QSAN_ENABLE_WEBSOCKETS=0
            QSAN_USE_RASTER_VIEWPORT=1
            WIN32
            WINVER=0x0501
            _WIN32_WINNT=0x0501
            _WIN32_IE=0x0600
        )
    else()
        target_compile_definitions(${QSAN_GUI_TARGET} PRIVATE
            QT_DISABLE_DEPRECATED_BEFORE=0x060000
            QT_QML_IMPORT_PATH="${QSAN_QT_QML_DIR}"
            HAS_QT_MULTIMEDIA
        )
    endif()

    if(WIN32)
        if(QSAN_BUILD_XP_LEGACY)
            target_compile_definitions(${QSAN_GUI_TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS)
        else()
            target_compile_definitions(${QSAN_GUI_TARGET} PRIVATE WIN64 _CRT_SECURE_NO_WARNINGS)
        endif()
    endif()

    if(MSVC)
        target_compile_options(${QSAN_GUI_TARGET} PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
            "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/bigobj;/MP8>"
        )
        if(QSAN_BUILD_XP_LEGACY)
            target_compile_options(${QSAN_GUI_TARGET} PRIVATE
                "/FI${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5/qsan-qt5-compat.h")
        endif()
    endif()

    target_precompile_headers(${QSAN_GUI_TARGET} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h>"
    )

    target_link_libraries(${QSAN_GUI_TARGET} PRIVATE
        qsanguosha_client_core
        ${QSAN_ENGINE_QT_LIBS}
        Qt6::Gui
        Qt6::Widgets
    )
    if(QSAN_BUILD_XP_LEGACY)
        # CMake's LINK_LIBRARY expansion emits GNU-style flags for this old
        # generator; use the native MSVC switch so static package registrars survive.
        target_link_libraries(${QSAN_GUI_TARGET} PRIVATE qsanguosha_engine)
        target_link_options(${QSAN_GUI_TARGET} PRIVATE
            "/WHOLEARCHIVE:$<TARGET_FILE:qsanguosha_engine>"
        )
    else()
        target_link_libraries(${QSAN_GUI_TARGET} PRIVATE
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
        )
    endif()
    if(NOT QSAN_BUILD_XP_LEGACY)
        target_link_libraries(${QSAN_GUI_TARGET} PRIVATE
            Qt6::Multimedia
            Qt6::OpenGLWidgets
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::QuickWidgets
        )
    endif()

    if(WIN32)
        if(QSAN_BUILD_XP_LEGACY)
            set(qsan_library_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x86")
            set(qsan_required_libraries freetype.lib fmodex.lib fmodexL.lib)
        else()
            set(qsan_library_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x64")
            set(qsan_required_libraries freetype.lib freetype_D.lib fmodex.lib)
        endif()
        foreach(required_library IN LISTS qsan_required_libraries)
            if(NOT EXISTS "${qsan_library_dir}/${required_library}")
                message(FATAL_ERROR "Missing bundled library: ${qsan_library_dir}/${required_library}")
            endif()
        endforeach()

        if(QSAN_BUILD_XP_LEGACY)
            target_link_libraries(${QSAN_GUI_TARGET} PRIVATE
                user32
                gdi32
                delayimp
                "$<$<CONFIG:Release>:dbghelp>"
                # The bundled debug archive imports post-XP debug CRT symbols;
                # Freetype's C ABI permits the x86 release archive in both modes.
                ${qsan_library_dir}/freetype.lib
                "$<$<CONFIG:Debug>:${qsan_library_dir}/fmodexL.lib>"
                "$<$<NOT:$<CONFIG:Debug>>:${qsan_library_dir}/fmodex.lib>"
            )
        else()
            target_link_libraries(${QSAN_GUI_TARGET} PRIVATE
                "$<$<CONFIG:Release>:user32>"
                "$<$<CONFIG:Release>:gdi32>"
                "$<$<CONFIG:Debug>:${qsan_library_dir}/freetype_D.lib>"
                "$<$<NOT:$<CONFIG:Debug>>:${qsan_library_dir}/freetype.lib>"
                dwmapi
                legacy_stdio_definitions
                winhttp
                "$<$<CONFIG:Release>:dbghelp>"
                "$<$<CONFIG:Release>:${qsan_library_dir}/fmodex.lib>"
            )
        endif()
    else()
        # Linux 使用系統 Freetype；不 link 任何 bundled *.lib，亦不需要 FMOD。
        find_package(Freetype REQUIRED)
        target_link_libraries(QSanguosha PRIVATE Freetype::Freetype)
    endif()

    if(MSVC)
        target_link_options(${QSAN_GUI_TARGET} PRIVATE
            "$<$<CONFIG:Release>:/DEBUG>"
            "$<$<BOOL:${QSAN_BUILD_XP_LEGACY}>:/SUBSYSTEM:WINDOWS,5.01>"
            "$<$<AND:$<BOOL:${QSAN_BUILD_XP_LEGACY}>,$<CONFIG:Debug>>:/NODEFAULTLIB:MSVCRT>"
            "$<$<AND:$<BOOL:${QSAN_BUILD_XP_LEGACY}>,$<CONFIG:Debug>>:/DELAYLOAD:fmodexL.dll>"
            "$<$<AND:$<BOOL:${QSAN_BUILD_XP_LEGACY}>,$<NOT:$<CONFIG:Debug>>>:/DELAYLOAD:fmodex.dll>"
        )
    endif()

    if(QSAN_BUILD_XP_LEGACY)
        set(qsan_gui_debug_output "${CMAKE_CURRENT_SOURCE_DIR}/xp-debug")
        set(qsan_gui_release_output "${CMAKE_CURRENT_SOURCE_DIR}/xp-release")
        set(qsan_gui_relwithdebinfo_output "${CMAKE_CURRENT_SOURCE_DIR}/xp-relwithdebinfo")
    else()
        set(qsan_gui_debug_output "${CMAKE_CURRENT_SOURCE_DIR}/debug")
        set(qsan_gui_release_output "${CMAKE_CURRENT_SOURCE_DIR}/release")
        set(qsan_gui_relwithdebinfo_output "${CMAKE_CURRENT_SOURCE_DIR}/relwithdebinfo")
    endif()
    set_target_properties(${QSAN_GUI_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${qsan_gui_debug_output}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${qsan_gui_release_output}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${qsan_gui_relwithdebinfo_output}"
        PDB_OUTPUT_DIRECTORY_DEBUG "${qsan_gui_debug_output}"
        PDB_OUTPUT_DIRECTORY_RELEASE "${qsan_gui_release_output}"
        PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${qsan_gui_relwithdebinfo_output}"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(NOT QSAN_BUILD_XP_LEGACY)
        set_target_properties(${QSAN_GUI_TARGET} PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    endif()

    if(QSAN_BUILD_XP_LEGACY)
        set(QSAN_XP_ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH
            "Asset root copied into the portable XP folder")
        set(QSAN_XP_FMOD_RUNTIME_DEBUG "" CACHE FILEPATH
            "Path to the x86 fmodexL.dll used by XP Debug deployment")
        set(QSAN_XP_FMOD_RUNTIME_RELEASE "" CACHE FILEPATH
            "Path to the x86 fmodex.dll used by XP Release deployment")
        set(QSAN_XP_QT_ROOT "${QT_PREFIX}" CACHE PATH
            "Qt 5.6.3 XP installation copied into the portable XP folder")
        set(QSAN_XP_VC_REDIST_DIR "" CACHE PATH
            "x86 Microsoft.VC141.CRT app-local runtime directory")
        set(QSAN_XP_UCRT_REDIST_DIR "" CACHE PATH
            "x86 Universal CRT app-local runtime directory")
        set(QSAN_XP_DEPLOY_ROOT "" CACHE PATH
            "Optional portable XP output directory outside the build output")
        add_custom_target(deploy-xp
            COMMAND ${CMAKE_COMMAND}
                -DQSAN_CONFIG=$<CONFIG>
                -DQSAN_EXECUTABLE=$<TARGET_FILE:${QSAN_GUI_TARGET}>
                -DQSAN_ASSET_ROOT=${QSAN_XP_ASSET_ROOT}
                -DQSAN_QT_ROOT=${QSAN_XP_QT_ROOT}
                -DQSAN_VC_REDIST_DIR=${QSAN_XP_VC_REDIST_DIR}
                -DQSAN_UCRT_REDIST_DIR=${QSAN_XP_UCRT_REDIST_DIR}
                -DQSAN_DEPLOY_ROOT=${QSAN_XP_DEPLOY_ROOT}
                -DQSAN_FMOD_RUNTIME_DEBUG=${QSAN_XP_FMOD_RUNTIME_DEBUG}
                -DQSAN_FMOD_RUNTIME_RELEASE=${QSAN_XP_FMOD_RUNTIME_RELEASE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/cmake/DeployXP.cmake
            DEPENDS ${QSAN_GUI_TARGET}
            VERBATIM
        )
    endif()

    if(NOT QSAN_BUILD_XP_LEGACY)
        set(translation_source "${CMAKE_CURRENT_SOURCE_DIR}/builds/sanguosha.ts")
        set_source_files_properties("${translation_source}" PROPERTIES
            OUTPUT_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        qt_add_lrelease(${QSAN_GUI_TARGET}
            TS_FILES "${translation_source}"
            QM_FILES_OUTPUT_VARIABLE qsan_qm_files
        )

        qt_add_lupdate(${QSAN_GUI_TARGET}
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
                qml/home/CardScene.qml
                qml/home/CardBrowserTile.qml
                qml/home/CardComboBox.qml
                qml/home/CardFilterPanel.qml
                qml/home/CardTagChip.qml
                qml/home/CardDetailPanel.qml
                qml/home/CardVariantList.qml
                qml/home/CardPagination.qml
                qml/home/HomeTheme.qml
        )
    endif()

    if(WIN32 AND NOT QSAN_BUILD_XP_LEGACY)
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
                -DQSAN_EXECUTABLE=$<TARGET_FILE:${QSAN_GUI_TARGET}>
                -DQSAN_WINDEPLOYQT=${QSAN_WINDEPLOYQT}
                -DQSAN_QML_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DQSAN_FMOD_RUNTIME=${QSAN_FMOD_RUNTIME}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Deploy.cmake
            DEPENDS ${QSAN_GUI_TARGET}
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

if(UNIX AND QSAN_BUILD_GUI)
    set_target_properties(QSanguosha PROPERTIES
        INSTALL_RPATH "$ORIGIN/${QSAN_BIN_TO_QT_LIB_RELATIVE}"
    )
    install(TARGETS QSanguosha
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT qsan_gui
    )
    # 同 shared data 一樣:有先裝,冇就由資產清單報「required 缺失」,
    # 而唔係喺 install 中途 fatal。package smoke 會因此紅燈。
    set(QSAN_GUI_DATA_DIRECTORIES "")
    foreach(qsan_gui_directory qss skins ui-script)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${qsan_gui_directory}")
            list(APPEND QSAN_GUI_DATA_DIRECTORIES ${qsan_gui_directory})
        endif()
    endforeach()
    if(QSAN_GUI_DATA_DIRECTORIES)
        install(DIRECTORY ${QSAN_GUI_DATA_DIRECTORIES}
            DESTINATION "${QSAN_DATA_INSTALL_DIR}"
            COMPONENT qsan_gui
        )
    endif()
    # 翻譯放喺 share/qsanguosha/translations/;resolver 會由嗰度載,搵唔到
    # 先返 asset root(舊有部署嘅 .qm 就係擺喺 root)。
    install(FILES ${qsan_qm_files} qt_zh_CN.qm
        DESTINATION "${QSAN_DATA_INSTALL_DIR}/translations"
        COMPONENT qsan_gui
        OPTIONAL
    )
    install(FILES packaging/linux/qsanguosha.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        COMPONENT qsan_gui
    )
    install(FILES resource/icon/linux/qsanguosha.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        COMPONENT qsan_gui
    )
    foreach(qsan_icon_size 16 24 32 48 64 128 256 512)
        install(FILES "resource/icon/linux/qsanguosha-${qsan_icon_size}.png"
            DESTINATION
                "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${qsan_icon_size}x${qsan_icon_size}/apps"
            RENAME "qsanguosha.png"
            COMPONENT qsan_gui
        )
    endforeach()
    install(FILES docs/linux-packaging.md
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        COMPONENT qsan_gui
    )
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
