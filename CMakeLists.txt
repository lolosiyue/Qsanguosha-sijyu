cmake_minimum_required(VERSION 3.28)

project(QSanguosha LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This CMake build currently supports Windows x64 with MSVC only")
endif()
if(MSVC_VERSION LESS 1920 OR MSVC_VERSION GREATER_EQUAL 1930)
    message(FATAL_ERROR "This CMake build currently supports the Visual Studio 2019 toolchain only")
endif()

enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Core5Compat
    Gui
    LinguistTools
    Multimedia
    Network
    OpenGLWidgets
    Qml
    Quick
    QuickControls2
    QuickWidgets
    Widgets
)

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
    src/core/card.h
    src/core/ai-data-store.cpp
    src/core/ai-data-store.h
    src/core/engine.cpp
    src/core/engine.h
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
    src/server/ai-runtime.cpp
    src/server/ai-runtime.h
    src/server/basicai.cpp
    src/server/card-movement-service.cpp
    src/server/card-movement-service.h
    src/server/extra-turn-scheduler.cpp
    src/server/extra-turn-scheduler.h
    src/server/game-snapshot-service.cpp
    src/server/game-snapshot-service.h
    src/server/gamerule.cpp
    src/server/generalselector.cpp
    src/server/qtupnpportmapping.cpp
    src/server/request-coordinator.cpp
    src/server/request-coordinator.h
    src/server/room-notifier.cpp
    src/server/room-notifier.h
    src/server/room-runtime.cpp
    src/server/room-runtime.h
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
    WIN64
    _CRT_SECURE_NO_WARNINGS
    "$<$<CONFIG:Release>:QSG_CRASH_HANDLER>"
)

target_compile_options(qsanguosha_engine PRIVATE /utf-8 /bigobj /MP8)

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

add_executable(qsanguosha_engine_smoke
    tests/engine-smoke-test.cpp
    src/server/server-core.h
)
target_include_directories(qsanguosha_engine_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/src/package
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
)
target_link_libraries(qsanguosha_engine_smoke PRIVATE qsanguosha_engine Qt6::Core5Compat Qt6::Qml Qt6::Widgets
    "$<$<CONFIG:Release>:dbghelp>")

add_executable(qsanguosha_engine_self_bridge_axe_test
    tests/engine-self-bridge-axe-test.cpp
)
target_link_libraries(qsanguosha_engine_self_bridge_axe_test PRIVATE
    qsanguosha_engine
)

add_executable(qsanguosha_card_parse_regex_test tests/card-parse-regex-test.cpp)
target_link_libraries(qsanguosha_card_parse_regex_test PRIVATE Qt6::Core)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME qsanguosha_engine_smoke COMMAND qsanguosha_engine_smoke)
    set_tests_properties(qsanguosha_engine_smoke PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME qsanguosha_engine_self_bridge_axe
        COMMAND qsanguosha_engine_self_bridge_axe_test
    )
    set_tests_properties(qsanguosha_engine_self_bridge_axe PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(NAME qsanguosha_card_parse_regex_test COMMAND qsanguosha_card_parse_regex_test)

    add_executable(qsanguosha_lua_runtime_isolation_test
        tests/lua-runtime-isolation-test.cpp
        src/server/server-core.h
    )
    target_include_directories(qsanguosha_lua_runtime_isolation_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    )
    target_link_libraries(qsanguosha_lua_runtime_isolation_test PRIVATE
        qsanguosha_engine Qt6::Core5Compat Qt6::Qml Qt6::Widgets
    )
    add_test(NAME qsanguosha_lua_runtime_isolation
             COMMAND qsanguosha_lua_runtime_isolation_test)

    add_executable(qsanguosha_room_runtime_isolation_test
        tests/room-runtime-isolation-test.cpp
        src/server/server-core.h
    )
    target_include_directories(qsanguosha_room_runtime_isolation_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )
    target_link_libraries(qsanguosha_room_runtime_isolation_test PRIVATE
        qsanguosha_engine Qt6::Core5Compat Qt6::Qml Qt6::Widgets
        "$<$<CONFIG:Release>:dbghelp>"
    )
    add_test(NAME qsanguosha_room_runtime_isolation
             COMMAND qsanguosha_room_runtime_isolation_test)
    set_tests_properties(qsanguosha_room_runtime_isolation PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_executable(qsanguosha_player_ui_state_test
        tests/player-ui-state-test.cpp
        src/core/protocol/state/player-ui-state.cpp
    )
    target_include_directories(qsanguosha_player_ui_state_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    )
    target_link_libraries(qsanguosha_player_ui_state_test PRIVATE Qt6::Core)
    add_test(NAME qsanguosha_player_ui_state COMMAND qsanguosha_player_ui_state_test)

    add_executable(qsanguosha_protocol_messages_test
        tests/protocol-messages-test.cpp
        src/core/protocol/card-provenance-message.cpp
        src/core/protocol/skill-instance-message.cpp
        src/core/protocol/switch-context-message.cpp
        src/core/protocol/sync-pile-message.cpp
    )
    target_include_directories(qsanguosha_protocol_messages_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    )
    target_link_libraries(qsanguosha_protocol_messages_test PRIVATE Qt6::Core)
    add_test(NAME qsanguosha_protocol_messages COMMAND qsanguosha_protocol_messages_test)

    add_executable(qsanguosha_replay_game_state_protocol_test
        tests/replay-game-state/replay-game-state-test.cpp
        src/server/server-core.h
        src/util/replay-game-state.cpp
        src/util/replay-game-state.h
    )
    target_include_directories(qsanguosha_replay_game_state_protocol_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )
    target_link_libraries(qsanguosha_replay_game_state_protocol_test PRIVATE
        qsanguosha_engine Qt6::Core Qt6::Core5Compat
    )
    add_test(NAME qsanguosha_replay_game_state_protocol
             COMMAND qsanguosha_replay_game_state_protocol_test)

    add_executable(qsanguosha_room_notifier_test
        tests/room-notifier-test.cpp
        src/server/server-core.h
    )
    target_include_directories(qsanguosha_room_notifier_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )
    target_link_libraries(qsanguosha_room_notifier_test PRIVATE
        qsanguosha_engine Qt6::Core5Compat Qt6::Qml Qt6::Widgets
        "$<$<CONFIG:Release>:dbghelp>"
    )
    add_test(NAME qsanguosha_room_notifier COMMAND qsanguosha_room_notifier_test)
    set_tests_properties(qsanguosha_room_notifier PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_executable(qsanguosha_request_coordinator_test
        tests/request-coordinator-test.cpp
        src/server/server-core.h
    )
    target_include_directories(qsanguosha_request_coordinator_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )
    target_link_libraries(qsanguosha_request_coordinator_test PRIVATE
        qsanguosha_engine Qt6::Core5Compat Qt6::Qml Qt6::Widgets
        "$<$<CONFIG:Release>:dbghelp>"
    )
    add_test(NAME qsanguosha_request_coordinator COMMAND qsanguosha_request_coordinator_test)
    set_tests_properties(qsanguosha_request_coordinator PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_executable(qsanguosha_card_movement_service_test
        tests/move-classifier-ordering/move-classifier-ordering-test.cpp
        src/server/server-core.h
    )
    target_include_directories(qsanguosha_card_movement_service_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )
    target_link_libraries(qsanguosha_card_movement_service_test PRIVATE
        qsanguosha_engine Qt6::Core5Compat Qt6::Qml Qt6::Widgets
        "$<$<CONFIG:Release>:dbghelp>"
    )
    add_test(NAME qsanguosha_card_movement_service
             COMMAND qsanguosha_card_movement_service_test)
    set_tests_properties(qsanguosha_card_movement_service PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_executable(qsanguosha_extra_turn_scheduler_test
        tests/extra-turn-scheduler-test.cpp
        src/server/server-core.h
    )
    target_include_directories(qsanguosha_extra_turn_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    )
    target_link_libraries(qsanguosha_extra_turn_scheduler_test PRIVATE
        qsanguosha_engine Qt6::Core5Compat Qt6::Qml Qt6::Widgets
        "$<$<CONFIG:Release>:dbghelp>"
    )
    add_test(NAME qsanguosha_extra_turn_scheduler
             COMMAND qsanguosha_extra_turn_scheduler_test)
    set_tests_properties(qsanguosha_extra_turn_scheduler PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_executable(qsanguosha_photo_layout_fit tests/photo-layout-fit-test.cpp)
    target_include_directories(qsanguosha_photo_layout_fit PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    )
    add_test(NAME qsanguosha_photo_layout_fit COMMAND qsanguosha_photo_layout_fit)
endif()

add_executable(qsanguosha_server
    src/server-main.cpp
    src/server/server-core.h
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
    WIN64
    _CRT_SECURE_NO_WARNINGS
)
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
)

add_custom_target(deploy-server
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/lua"
        "$<TARGET_FILE_DIR:qsanguosha_server>/lua"
    DEPENDS qsanguosha_server
    VERBATIM
)

qt_add_executable(QSanguosha WIN32
    ${QSAN_SOURCES}
    ${QSAN_MOC_HEADERS}
    ${QSAN_FORMS}
    src/ui/package-dialogs.cpp
    src/ui/package-dialogs.h
    resource/icon.rc
)

target_sources(QSanguosha PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/home/home_qml.qrc"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/home/home_assets.qrc"
)

target_sources(QSanguosha PRIVATE
    "$<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/src/core/audio.cpp>"
)

target_include_directories(QSanguosha PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
    ${CMAKE_CURRENT_SOURCE_DIR}/include/spine-cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dialog
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/src/package
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/home
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    "$<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/include/fmod>"
)

target_compile_definitions(QSanguosha PRIVATE
    USING_PCH
    WIN64
    _CRT_SECURE_NO_WARNINGS
    QT_DEPRECATED_WARNINGS
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    QT_QML_IMPORT_PATH="${QT_PREFIX}/qml"
    QT_BIN_DIR="${QT_PREFIX}/bin"
    "$<$<CONFIG:Release>:AUDIO_SUPPORT>"
)

target_compile_options(QSanguosha PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
    "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/bigobj;/MP8>"
)

target_precompile_headers(QSanguosha PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h>"
)

set(qsan_library_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x64")
foreach(required_library freetype.lib freetype_D.lib fmodex.lib)
    if(NOT EXISTS "${qsan_library_dir}/${required_library}")
        message(FATAL_ERROR "Missing bundled library: ${qsan_library_dir}/${required_library}")
    endif()
endforeach()

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

target_compile_definitions(QSanguosha PRIVATE HAS_QT_MULTIMEDIA)

target_link_options(QSanguosha PRIVATE "$<$<CONFIG:Release>:/DEBUG>")

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
        ${QSAN_FORMS}
        qml/home/HomeScene.qml
        qml/home/HomeMainButton.qml
        qml/home/MainActionPanel.qml
        qml/home/HomeBottomBar.qml
        qml/home/HomeNavButton.qml
        qml/home/HomeSideBar.qml
)

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

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${QSAN_SOURCES}
    ${QSAN_LUA_SOURCES}
    ${QSAN_MOC_HEADERS}
    ${QSAN_FORMS}
)
source_group("Generated" FILES "${qsan_swig_wrapper}")
