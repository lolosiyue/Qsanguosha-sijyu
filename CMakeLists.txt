cmake_minimum_required(VERSION 3.28)

project(QSanguosha LANGUAGES C CXX)

# CTest is only used to run local and CI tests; CDash is never used. Besides the
# BUILD_TESTING option and enable_testing(), include(CTest) also spawns 28 extra
# Continuous*/Nightly*/Experimental* dashboard targets - Visual Studio turns each into
# a solution project that occupies over half the solution, so only the two needed
# pieces are used.
option(BUILD_TESTING "Build the testing tree." ON)
if(BUILD_TESTING)
    enable_testing()
endif()

# Visual Studio / Xcode: tuck the test targets and CMake's built-in ALL_BUILD /
# ZERO_CHECK / RUN_TESTS into a solution folder instead of flattening them at the
# root. Ninja / Makefile generators ignore these two properties.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")

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
if(WIN32 OR ANDROID)
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
elseif(ANDROID)
    set(QSAN_BUILD_SERVER_DEFAULT OFF)
else()
    set(QSAN_BUILD_SERVER_DEFAULT ON)
endif()
option(QSAN_BUILD_SERVER
    "Build the dedicated QSanguosha server"
    ${QSAN_BUILD_SERVER_DEFAULT}
)
if(ANDROID)
    set(QSAN_BUILD_TUI_DEFAULT OFF)
else()
    set(QSAN_BUILD_TUI_DEFAULT ON)
endif()
option(QSAN_BUILD_TUI
    "Build the Protocol V2 terminal client"
    ${QSAN_BUILD_TUI_DEFAULT}
)
option(QSAN_BUILD_EXCEL
    "Build the opt-in Windows Excel bridge"
    OFF
)
option(QSAN_BUILD_EXCEL_TESTS
    "Build opt-in native Excel focused test executables"
    OFF
)
if(QSAN_BUILD_EXCEL AND NOT WIN32)
    message(FATAL_ERROR "QSAN_BUILD_EXCEL is available on Windows only")
endif()
option(QSAN_BUILD_WASM_WEB_CLIENT
    "Build the persistent Web Worker client-rules runtime with Emscripten"
    OFF
)
option(QSAN_BUILD_WASM_SOLO
    "Build the browser-hosted single-player server Worker (multithreaded Qt)"
    OFF
)
if(QSAN_BUILD_WASM_SOLO)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "QSAN_BUILD_WASM_SOLO requires the Emscripten toolchain")
    endif()
    # pthread changes the entire ABI, including libraries compiled in this tree.
    # Use a separate wasm_multithread build directory, never the client-only kit.
    add_compile_options(-pthread)
    add_link_options(-pthread)
endif()

# Audio backend. A single variable decides which implementation the GUI client uses;
# call sites contain no platform #ifdef:
#
#   FMOD  Windows GUI Release (bundled fmodex, behaves exactly as before)
#   QT    Linux GUI (Qt Multimedia)
#   NULL  dedicated server / CI / any build that must stay silent
#
# The dedicated server does not compile in any audio files, so NULL is also the
# natural default for server-only builds.
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
elseif(NOT UNIX AND NOT ANDROID)
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

if(QSAN_BUILD_EXCEL AND QSAN_AUDIO_BACKEND STREQUAL "QT")
    list(APPEND QSAN_QT_COMPONENTS Multimedia)
endif()
if(QSAN_BUILD_EXCEL_TESTS)
    list(APPEND QSAN_QT_COMPONENTS Test)
endif()

list(REMOVE_DUPLICATES QSAN_QT_COMPONENTS)

# The Qt baseline of the GUI product is consistent between Windows and Linux. GUI
# sources use APIs only added in Qt 6.5 (e.g. QStyleHints::colorScheme()), and the
# production Windows toolchain is already Qt 6.11.1, so GUI builds explicitly require
# 6.11.
#
# The server-only build deliberately skips this floor: the dedicated server only needs
# Core/Network (plus WebSockets on Qt6) and must stay buildable against the
# distro-provided Qt 6.x (e.g. 6.4.2 on Ubuntu 24.04). XP/Qt 5.6.3 does not use WebSockets.
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

if(ANDROID)
    include(cmake/QSanguoshaAndroid.cmake)
endif()

set(QSAN_ENGINE_QT_LIBS Qt6::Core Qt6::Network)

if(ANDROID)
    # SWIG is a host executable even while the generated wrapper is compiled
    # for Android. Do not let the Android toolchain redirect this lookup.
    find_program(QSAN_SWIG_EXECUTABLE
        NAMES swig swig.exe
        HINTS
            "$ENV{QSAN_QT_HOST_PATH}/bin"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/swig"
        NO_CMAKE_FIND_ROOT_PATH
        REQUIRED
    )
else()
    find_program(QSAN_SWIG_EXECUTABLE
        NAMES swig swig.exe
        HINTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/swig"
        REQUIRED
    )
endif()

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
include(cmake/QSanguoshaAndroidAssets.cmake)

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
    list(APPEND QSAN_SOURCES legacy/xp/src/local-server-controller.cpp)
    list(APPEND QSAN_MOC_HEADERS legacy/xp/src/local-server-controller.h)

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
    src/core/replay-takeover-validation.cpp
    src/core/replay-takeover-validation.h
    src/core/banpair.cpp
    src/core/banpair.h
    src/core/card.cpp
    src/core/card-lifetime-manager.cpp
    src/core/card-lifetime-manager.h
    src/core/card.h
    src/core/skill-set-generation.h
    src/core/ai-probe.cpp
    src/core/ai-probe.h
    src/core/ai-data-store.cpp
    src/core/ai-data-store.h
    src/core/asset-manifest.cpp
    src/core/asset-manifest.h
    src/core/rules-content-manifest.cpp
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
    src/core/rules-bundle-exporter.cpp
    src/core/rules-bundle-exporter.h
    src/core/protocol/rules-bundle-identity.cpp
    src/core/protocol/rules-bundle-identity.h
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
    src/util/websocket-gateway.cpp
    src/util/websocket-gateway.h
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

if(ANDROID)
    target_sources(qsanguosha_engine PRIVATE
        src/core/android_assets.cpp
        src/core/android_assets.h
        src/core/android-content-store.cpp
        src/core/android-content-store.h
        src/core/android-zip-reader.cpp
        src/core/android-zip-reader.h
    )
    target_link_libraries(qsanguosha_engine PUBLIC z)
endif()

set_source_files_properties(${QSAN_LUA_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
list(REMOVE_ITEM QSAN_SOURCES src/util/game-snapshot.cpp)
list(REMOVE_ITEM QSAN_SOURCES src/client/aux-skills.cpp)
list(REMOVE_ITEM QSAN_MOC_HEADERS src/util/game-snapshot.h src/util/socket.h src/client/aux-skills.h)

target_include_directories(qsanguosha_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    # TUs from package/scenario include client.h, and client.h now sees ClientCore.
    # This is a header-only dependency: the engine neither links qsanguosha_client_core
    # nor calls any of its symbols.
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

include(cmake/QSanguoshaRulesIdentity.cmake)

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

target_link_libraries(qsanguosha_engine PUBLIC ${QSAN_ENGINE_QT_LIBS})

if(QSAN_ENABLE_WEBSOCKETS)
    add_library(qsanguosha_websocket STATIC
        src/util/websocketsocket.cpp
        src/util/websocketsocket.h
    )
    set_target_properties(qsanguosha_websocket PROPERTIES
        AUTOMOC ON
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(qsanguosha_websocket PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )
    target_compile_definitions(qsanguosha_websocket PRIVATE
        QSAN_ENGINE_BUILD
        QSAN_SERVER_CORE_ONLY
    )
    target_link_libraries(qsanguosha_websocket PUBLIC
        Qt6::WebSockets
    )
    # The engine only provides a compile usage requirement; it must not become a link
    # item. A static library's PRIVATE still reaches consumers as $<LINK_ONLY:>, while
    # the executable side is $<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>; one link
    # item carrying two features makes CMake 3.28 (the apt cmake on Ubuntu 24.04 /
    # Docker builders) fatal with "Impossible to link target ... has already occurred
    # with the feature 'WHOLE_ARCHIVE'". Newer CMake relaxed this, so the preinstalled
    # cmake on GitHub runners never sees it; only Docker CI goes red. $<COMPILE_ONLY:>
    # cuts the propagation; linking the real engine remains the final executable's job.
    target_link_libraries(qsanguosha_websocket PRIVATE
        "$<COMPILE_ONLY:qsanguosha_engine>"
    )
    if(MSVC)
        target_compile_definitions(qsanguosha_websocket PRIVATE WIN64 _CRT_SECURE_NO_WARNINGS)
        target_compile_options(qsanguosha_websocket PRIVATE /utf-8 /bigobj /MP8)
    endif()
endif()

function(qsan_link_websocket_gateway target)
    if(QSAN_ENABLE_WEBSOCKETS)
        target_link_libraries(${target} PRIVATE qsanguosha_websocket)
    endif()
endfunction()

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

# Client Architecture F1: ClientCore.
#
# The middle layer is its own static library instead of a few .cpp files inside the
# GUI target, so that "ClientCore does not depend on the GUI" is enforced by the
# build system rather than by discipline: the allowlist gate below only permits
# Qt6::Core, so anyone trying to include QWidget/QQuickItem/Dashboard/RoomScene from
# ClientCore blows up at configure or link time.
#
# It also deliberately does not link qsanguosha_engine: the interaction model only
# speaks card ids, player objectNames and option strings; the rules truth stays on
# the server. The dedicated server build creates this target too, so a server-only
# configure also verifies it has no GUI dependency.
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
    src/client/core/client-prompt.cpp
    src/client/core/client-prompt.h
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

# Shared live session/request layer for TUI and native adapters. Keep all
# presentation code in its consumers so the Excel bridge never links TUI.
add_library(qsanguosha_client_session STATIC
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
)
set_target_properties(qsanguosha_client_session PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
    FOLDER "Libraries"
)
target_include_directories(qsanguosha_client_session PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
)
target_compile_definitions(qsanguosha_client_session PRIVATE QSAN_ENGINE_TEST_BUILD)
target_link_libraries(qsanguosha_client_session PUBLIC
    qsanguosha_client_core
    qsanguosha_client_runtime
    Qt6::Core
    Qt6::Network
)
target_link_libraries(qsanguosha_client_session PRIVATE
    "$<COMPILE_ONLY:qsanguosha_engine>"
)
if(QSAN_BUILD_XP_LEGACY)
    target_include_directories(qsanguosha_client_session BEFORE PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5
    )
    target_compile_definitions(qsanguosha_client_session PRIVATE
        QSAN_XP_LEGACY WIN32 WINVER=0x0501 _WIN32_WINNT=0x0501)
endif()

if(MSVC)
    target_compile_definitions(qsanguosha_client_session PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(qsanguosha_client_session PRIVATE /utf-8 /bigobj /MP8)
    if(QSAN_BUILD_XP_LEGACY)
        target_compile_options(qsanguosha_client_session PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/compat/qt5/qsan-qt5-compat.h")
    endif()
endif()

if(QSAN_BUILD_TUI)
    add_library(qsanguosha_tui_support STATIC
        src/tui/tui-application-controller.cpp
        src/tui/tui-application-controller.h
        src/tui/tui-board-layout.cpp
        src/tui/tui-board-layout.h
        src/tui/tui-board-presenter.cpp
        src/tui/tui-board-presenter.h
        src/tui/tui-board-view.cpp
        src/tui/tui-board-view.h
        src/tui/tui-card-text.cpp
        src/tui/tui-card-text.h
        src/tui/tui-client-player.cpp
        src/tui/tui-client-player.h
        src/tui/tui-command.cpp
        src/tui/tui-command.h
        src/tui/tui-log-text.cpp
        src/tui/tui-log-text.h
        src/tui/tui-play-skills.cpp
        src/tui/tui-play-skills.h
        src/tui/tui-presenter.h
        src/tui/tui-skill-dialog.cpp
        src/tui/tui-skill-dialog.h
        src/tui/tui-stream-presenter.cpp
        src/tui/tui-stream-presenter.h
        src/tui/tui-synthesized-log.cpp
        src/tui/tui-synthesized-log.h
        src/tui/tui-target-advice.cpp
        src/tui/tui-target-advice.h
        src/tui/tui-terminal.cpp
        src/tui/tui-terminal.h
        src/tui/tui-text-width.cpp
        src/tui/tui-text-width.h
        src/tui/tui-text.cpp
        src/tui/tui-text.h
        src/tui/tui-ui-mode.cpp
        src/tui/tui-ui-mode.h
        src/tui/tui-input.cpp
        src/tui/tui-input.h
        src/tui/tui-line-editor.cpp
        src/tui/tui-line-editor.h
        src/tui/tui-interaction-view.cpp
        src/tui/tui-interaction-view.h
        src/tui/tui-renderer.cpp
        src/tui/tui-renderer.h
        src/tui/tui-resolvers.h
        src/tui/tui-room-context.cpp
        src/tui/tui-room-context.h
        src/tui/tui-screen.cpp
        src/tui/tui-screen.h
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
        qsanguosha_client_session
        Qt6::Core
        Qt6::Network
    )
    # The engine only provides a compile usage requirement; it must not become a link
    # item. A static library's PRIVATE still reaches consumers as $<LINK_ONLY:>, while
    # the executable side is $<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>; one link
    # item carrying two features makes CMake 3.28 (the apt cmake on Ubuntu 24.04 /
    # Docker builders) fatal with "Impossible to link target ... has already occurred
    # with the feature 'WHOLE_ARCHIVE'". Newer CMake relaxed this, so the preinstalled
    # cmake on GitHub runners never sees it; only Docker CI goes red. $<COMPILE_ONLY:>
    # cuts the propagation; linking the real engine remains the final executable's job.
    target_link_libraries(qsanguosha_tui_support PRIVATE
        "$<COMPILE_ONLY:qsanguosha_engine>"
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
    # TUI is a TCP client. Do not qsan_link_websocket_gateway() here.
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

set(QSAN_DEDICATED_ENTRY_SOURCES
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
if(QSAN_BUILD_XP_LEGACY)
    include(legacy/xp/cmake/XPServer.cmake)
endif()
if(QSAN_BUILD_EXCEL)
    include(cmake/QSanguoshaExcel.cmake)
endif()
if(QSAN_BUILD_SERVER)
    add_executable(qsanguosha_server ${QSAN_DEDICATED_ENTRY_SOURCES})
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
    qsan_link_websocket_gateway(qsanguosha_server)
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
# Three install components so the "GUI package" and the "server package" can ship
# separately:
#
#   qsan_data    lua / extensions / lang / etc - rules data needed by both sides
#   qsan_server  dedicated server binary, systemd unit, server docs
#   qsan_gui     GUI binary, UI assets, desktop entry, icons
#
# A server-only install therefore pulls in neither GUI assets nor the Qt GUI runtime.
# ---------------------------------------------------------------------------
if(UNIX AND NOT ANDROID)
    set(QSAN_DATA_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/qsanguosha")

    # Relative path from bin/ to share/qsanguosha/. The runtime resolver uses this
    # instead of the absolute CMAKE_INSTALL_FULL_DATADIR: a binary must not bake in
    # the installing machine's paths, and the portable bundle / AppImage must work
    # when unpacked anywhere.
    file(RELATIVE_PATH QSAN_BIN_TO_DATA_RELATIVE
        "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_DATADIR}/qsanguosha"
    )
    # Relative path from bin/ to the private Qt runtime. Qt's own library/plugin/QML
    # plugins already carry $ORIGIN-relative RUNPATH and assume the standard prefix
    # layout (lib/, plugins/<type>/, qml/<Module>/ all under $ORIGIN/../../lib), so the
    # private runtime must replicate this layout - then no patchelf is needed to
    # rewrite every Qt .so.
    set(QSAN_PRIVATE_QT_DIR "${CMAKE_INSTALL_LIBDIR}/qsanguosha/qt")
    file(RELATIVE_PATH QSAN_BIN_TO_QT_LIB_RELATIVE
        "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_LIBDIR}/qsanguosha/qt/lib"
    )

    target_compile_definitions(qsanguosha_engine PRIVATE
        QSAN_BIN_TO_DATA_RELATIVE="${QSAN_BIN_TO_DATA_RELATIVE}"
    )

    # Asset manifest: what actually exists in the install tree that is "cannot run
    # without it" vs "missing it only loses some sound/art". GUI-only assets are listed
    # as required only for GUI builds. Not every build context has all of these
    # directories: the Docker server image's .dockerignore deliberately excludes the
    # GUI/client assets (qss/, skins/, ui-script/), while extensions/ and lua/ai/ are
    # fetched during the build. install(DIRECTORY) fatals outright on a missing
    # directory, so each is checked for existence before installing; "should exist but
    # does not" is loudly reported by the asset manifest instead of exploding mid-install.
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
    # A missing lang/ does not crash - sgs.GetFileNames() returns an empty list for a
    # non-existent directory, the game keeps running and just shows internal names
    # instead of Chinese. The dedicated server (Docker image) has always run that way.
    # But a GUI without translations is not usable for players, so GUI builds treat it
    # as required.
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
            # Home page QML icons and backdrops: not stored in the repository, so a GUI
            # launched from a clean checkout always shows a batch of "QML Image: Cannot
            # open" warnings. Listing them here turns those warnings from "unknown
            # origin" into "expected-missing as documented by the manifest".
            qml/home/icons
            image/system/backdrop
        )
    endif()

    # Version fields: game_version follows src/core/version.h; asset_pack_version
    # describes which asset set this core package carries (large art/audio excluded).
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
    # Deliberately not written into the source tree: the GUI build and the server-only
    # build have different required lists, and two build directories would overwrite
    # the same file, making the dev tree's --asset-report a coin toss as to which build
    # it describes. A missing manifest in the dev tree is the normal state;
    # QSanAssetManifest::inspect() honestly reports "cannot verify" instead of erroring.

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

if(UNIX AND NOT ANDROID AND QSAN_BUILD_SERVER)
    set(QSAN_SYSTEMD_SERVICE_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/qsanguosha-server.service")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/qsanguosha-server.service.in"
        "${QSAN_SYSTEMD_SERVICE_FILE}"
        @ONLY
    )
    # The private Qt runtime does not exist at all in a distro-style install; there the
    # loader falls straight through to the system Qt. In the portable/AppImage layouts
    # it hits the bundled copy instead. The same binary is correct in both cases.
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
    elseif(ANDROID)
        set(QSAN_GUI_TARGET QSanguosha)
        # Keep Android target creation in its own directory. The target must
        # use MANUAL_FINALIZATION so all resources, links and QML roots below
        # are present before Qt generates deployment settings.
        add_subdirectory(cmake/android-app)
    else()
        set(QSAN_GUI_TARGET QSanguosha)
        qt_add_executable(${QSAN_GUI_TARGET}
            ${QSAN_GUI_SOURCES}
        )
    endif()

    if(ANDROID)
        target_sources(${QSAN_GUI_TARGET} PRIVATE
            src/dialog/android-content-dialog.cpp
            src/dialog/android-content-dialog.h
            src/dialog/android-dialog-fit.cpp
            src/dialog/android-dialog-fit.h
        )
        qsan_configure_android_target(${QSAN_GUI_TARGET})
        qsan_add_android_runtime_assets(${QSAN_GUI_TARGET})
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
            # The bundled fmodex.lib only links in Release, so the FMOD implementation is
            # only compiled in for Release as well; Debug falls back to the null backend,
            # silent just like the old "Debug has no AUDIO_SUPPORT".
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
        # The bundled Freetype/FMOD/vld headers are Windows-only; putting them on the
        # Linux include path would shadow the system Freetype headers (ft2build.h mixed
        # with <freetype/*.h> of a different version).
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
        # The facade exists in every GUI config (worst case the null backend), so
        # AUDIO_SUPPORT now means "this target has an audio facade" instead of
        # "this target can reach FMOD". The engine/server targets do not define it
        # either; as before they pull in no audio implementation.
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
        qsanguosha_client_session
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
    qsan_link_websocket_gateway(${QSAN_GUI_TARGET})
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
    elseif(ANDROID)
        # FreeType is supplied by the Android toolchain and must remain a
        # static target; never search the host's desktop installation.
        target_link_libraries(${QSAN_GUI_TARGET} PRIVATE Freetype::Freetype)
    else()
        # Linux 使用系統 Freetype；不 link 任何 bundled *.lib，亦不需要 FMOD。
        find_package(Freetype REQUIRED)
        target_link_libraries(${QSAN_GUI_TARGET} PRIVATE Freetype::Freetype)
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
    elseif(ANDROID)
        # Android qt_add_executable() creates a shared MODULE in the build
        # tree. Keep its ABI-specific output beside the Ninja build files so
        # androiddeployqt can package it without touching the source tree.
        set(qsan_gui_debug_output "${CMAKE_CURRENT_BINARY_DIR}")
        set(qsan_gui_release_output "${CMAKE_CURRENT_BINARY_DIR}")
        set(qsan_gui_relwithdebinfo_output "${CMAKE_CURRENT_BINARY_DIR}")
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
        target_include_directories(${QSAN_GUI_TARGET} PRIVATE "${CMAKE_SOURCE_DIR}/legacy/xp/src")
        target_link_libraries(${QSAN_GUI_TARGET} PRIVATE qsan_xp_control)
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
                -DQSAN_SERVER_EXECUTABLE=$<TARGET_FILE:QSanguoshaXPServer>
                -DQSAN_ASSET_ROOT=${QSAN_XP_ASSET_ROOT}
                -DQSAN_QT_ROOT=${QSAN_XP_QT_ROOT}
                -DQSAN_VC_REDIST_DIR=${QSAN_XP_VC_REDIST_DIR}
                -DQSAN_UCRT_REDIST_DIR=${QSAN_XP_UCRT_REDIST_DIR}
                -DQSAN_DEPLOY_ROOT=${QSAN_XP_DEPLOY_ROOT}
                -DQSAN_FMOD_RUNTIME_DEBUG=${QSAN_XP_FMOD_RUNTIME_DEBUG}
                -DQSAN_FMOD_RUNTIME_RELEASE=${QSAN_XP_FMOD_RUNTIME_RELEASE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/legacy/xp/cmake/DeployXP.cmake
            DEPENDS ${QSAN_GUI_TARGET} QSanguoshaXPServer
            VERBATIM
        )
    endif()

    if(NOT QSAN_BUILD_XP_LEGACY)
        set(translation_source "${CMAKE_CURRENT_SOURCE_DIR}/builds/sanguosha.ts")
        if(ANDROID)
            set(qsan_translation_output "${CMAKE_CURRENT_BINARY_DIR}/translations")
        else()
            set(qsan_translation_output "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
        set_source_files_properties("${translation_source}" PROPERTIES
            OUTPUT_LOCATION "${qsan_translation_output}"
        )
        qt_add_lrelease(${QSAN_GUI_TARGET}
            TS_FILES "${translation_source}"
            QM_FILES_OUTPUT_VARIABLE qsan_qm_files
        )

        if(ANDROID)
            get_filename_component(qsan_android_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
            file(GLOB qsan_android_qt_translations
                "${qsan_android_qt_prefix}/translations/qt*_zh_CN.qm")
            set(qsan_android_translations ${qsan_qm_files} ${qsan_android_qt_translations})
            foreach(qsan_android_translation IN LISTS qsan_android_translations)
                get_filename_component(qsan_android_translation_name "${qsan_android_translation}" NAME)
                set_source_files_properties("${qsan_android_translation}" PROPERTIES
                    QT_RESOURCE_ALIAS "${qsan_android_translation_name}")
            endforeach()
            qt_add_resources(${QSAN_GUI_TARGET} qsan_android_translations
                PREFIX "/assets/translations"
                FILES ${qsan_android_translations})
        endif()

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

    if(ANDROID)
        # qt_add_executable() was declared with MANUAL_FINALIZATION in the
        # Android child directory. Finalize only after the complete GUI graph
        # (qrc resources, audio, translations and links) is assembled.
        qt_finalize_target(${QSAN_GUI_TARGET})
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

if(UNIX AND NOT ANDROID AND QSAN_BUILD_GUI)
    set_target_properties(QSanguosha PROPERTIES
        INSTALL_RPATH "$ORIGIN/${QSAN_BIN_TO_QT_LIB_RELATIVE}"
    )
    install(TARGETS QSanguosha
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT qsan_gui
    )
    # Same as the shared data: install only when present, otherwise the asset manifest
    # reports a "required missing" entry instead of a fatal error mid-install. The
    # package smoke then goes red because of it.
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
    # Translations live in share/qsanguosha/translations/; the resolver loads from
    # there and only falls back to the asset root (legacy deployments place their .qm
    # files at the root).
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

# Visual Studio solution grouping: the three product executables stay at the root and
# the intermediate static libraries are tucked away. if(TARGET) is used because
# server-only / XP legacy configurations do not build all of them.
foreach(qsan_library
    qsanguosha_engine
    qsanguosha_websocket
    qsanguosha_client_core
    qsanguosha_tui_support
)
    if(TARGET ${qsan_library})
        set_property(TARGET ${qsan_library} PROPERTY FOLDER "Libraries")
    endif()
endforeach()
