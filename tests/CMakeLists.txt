set(QSAN_TEST_ROOT "${CMAKE_SOURCE_DIR}")
# All targets defined in this directory are filed under the Visual Studio "Tests" solution folder.
set(CMAKE_FOLDER "Tests")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(QSAN_ENGINE_TEST_INCLUDES
    ${QSAN_TEST_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${QSAN_TEST_ROOT}/include
    ${QSAN_TEST_ROOT}/include/freetype
    ${QSAN_TEST_ROOT}/src/client
    ${QSAN_TEST_ROOT}/src/client/core
    ${QSAN_TEST_ROOT}/src/core
    ${QSAN_TEST_ROOT}/src/lua
    ${QSAN_TEST_ROOT}/src/package
    ${QSAN_TEST_ROOT}/src/scenario
    ${QSAN_TEST_ROOT}/src/server
    ${QSAN_TEST_ROOT}/src/util
)

function(qsan_link_engine_test target)
    target_include_directories(${target} PRIVATE ${QSAN_ENGINE_TEST_INCLUDES})
    target_compile_definitions(${target} PRIVATE QSAN_ENGINE_TEST_BUILD)
    target_link_libraries(${target} PRIVATE
        qsanguosha_engine
        Qt6::Gui
    )
    if(WIN32)
        target_link_libraries(${target} PRIVATE "$<$<CONFIG:Release>:dbghelp>")
    endif()
endfunction()

function(qsan_add_ctest name executable)
    cmake_parse_arguments(ARG "RUN_SERIAL" "SUITE;TIMEOUT" "LABELS;ARGS" ${ARGN})
    set(qsan_test_arguments)
    if(ARG_SUITE)
        list(APPEND qsan_test_arguments --suite ${ARG_SUITE})
    endif()
    list(APPEND qsan_test_arguments ${ARG_ARGS})
    add_test(NAME ${name} COMMAND ${executable} ${qsan_test_arguments})
    set_tests_properties(${name} PROPERTIES
        WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
        LABELS "${ARG_LABELS}"
    )
    if(ARG_TIMEOUT)
        set_property(TEST ${name} PROPERTY TIMEOUT ${ARG_TIMEOUT})
    endif()
    if(ARG_RUN_SERIAL)
        set_property(TEST ${name} PROPERTY RUN_SERIAL TRUE)
    endif()
    if(WIN32)
        set_property(TEST ${name} PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endfunction()

include("${QSAN_TEST_ROOT}/cmake/QSanguoshaAndroidContentTests.cmake")

# All tests that link the engine + Qt6::Gui live in one executable. Their link
# surface is identical (qsan_link_engine_test); keeping them separate would
# only add seven Visual Studio projects. Each suite still runs in its own
# process: the merged-in suites keep their original main(), renamed via
# COMPILE_DEFINITIONS main=..., without a single line changed in the test files.
add_executable(qsanguosha_core_tests
    core-tests-main.cpp
    engine-smoke-test.cpp
    lua-compat-test.cpp
    core-regressions-test.cpp
    equips-nullified-test.cpp
    package-policy-test.cpp
    package-ownership-test.cpp
    card-overview-classifier-test.cpp
    card-overview-model-test.cpp
    skill_ui_runner/local-response-ui-case-test.cpp
    runtime-paths-test.cpp
    replay-game-state/replay-game-state-test.cpp
    takeover-snapshot/takeover-snapshot-test.cpp
    photo-layout-fit-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/card-overview-data.cpp
    ${QSAN_TEST_ROOT}/src/ui/card-overview-data.h
    ${QSAN_TEST_ROOT}/src/ui/home/homecardmodel.cpp
    ${QSAN_TEST_ROOT}/src/ui/home/homecardmodel.h
    ${QSAN_TEST_ROOT}/src/ui/testing/local-response-ui-case.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/local-response-ui-case.h
    ${QSAN_TEST_ROOT}/src/util/replay-game-state.cpp
    ${QSAN_TEST_ROOT}/src/util/replay-game-state.h
    ${QSAN_TEST_ROOT}/src/core/replay/replay-container.cpp
    ${QSAN_TEST_ROOT}/src/util/recorder.cpp
    ${QSAN_TEST_ROOT}/src/util/replay-index.cpp
    # takeover-snapshot-test.cpp calls ReplayDiagnosticExporter (added by cb5340a on the
    # other side of merge 7fdf0c6); the fold in bf1f85d never picked this source up.
    ${QSAN_TEST_ROOT}/src/util/replay-diagnostic-exporter.cpp
)

# Keep the Android extraction regression small and deterministic.  These files
# model the qrc prefix used by the Android product without pulling the full
# media package into the core test executable.
set(QSAN_ANDROID_ASSET_FIXTURE_ROOT
    "${QSAN_TEST_ROOT}/tests/fixtures/android-assets")
set_source_files_properties(
    "${QSAN_ANDROID_ASSET_FIXTURE_ROOT}/config.fixture"
    PROPERTIES QT_RESOURCE_ALIAS "config.ini")
set_source_files_properties(
    "${QSAN_ANDROID_ASSET_FIXTURE_ROOT}/extension.fixture"
    PROPERTIES QT_RESOURCE_ALIAS "extensions/android_fixture.lua")
set_source_files_properties(
    "${QSAN_ANDROID_ASSET_FIXTURE_ROOT}/nested-extension.fixture"
    PROPERTIES QT_RESOURCE_ALIAS "extensions/nested/child.lua")
set_source_files_properties(
    "${QSAN_ANDROID_ASSET_FIXTURE_ROOT}/ai.fixture"
    PROPERTIES QT_RESOURCE_ALIAS "lua/ai/android_fixture-ai.lua")
qt_add_resources(qsanguosha_core_tests qsan_android_asset_fixtures
    PREFIX "/assets"
    BASE "${QSAN_ANDROID_ASSET_FIXTURE_ROOT}"
    FILES
        "${QSAN_ANDROID_ASSET_FIXTURE_ROOT}/config.fixture"
        "${QSAN_ANDROID_ASSET_FIXTURE_ROOT}/extension.fixture"
        "${QSAN_ANDROID_ASSET_FIXTURE_ROOT}/nested-extension.fixture"
        "${QSAN_ANDROID_ASSET_FIXTURE_ROOT}/ai.fixture"
)
if(NOT ANDROID)
    target_sources(qsanguosha_core_tests PRIVATE
        ${QSAN_TEST_ROOT}/src/core/android_assets.cpp)
endif()
set_source_files_properties(card-overview-classifier-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runCardOverviewClassifierTests)
set_source_files_properties(card-overview-model-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runCardOverviewModelTests)
set_source_files_properties(skill_ui_runner/local-response-ui-case-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runLocalResponseUiCaseTests)
set_source_files_properties(runtime-paths-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runRuntimePathsTests)
set_source_files_properties(replay-game-state/replay-game-state-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runReplayGameStateTests)
set_source_files_properties(takeover-snapshot/takeover-snapshot-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runTakeoverSnapshotTests)
set_source_files_properties(photo-layout-fit-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runPhotoLayoutFitTests)
set_source_files_properties(equips-nullified-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runEquipsNullifiedTests)
qsan_link_engine_test(qsanguosha_core_tests)
target_include_directories(qsanguosha_core_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/ui
    ${QSAN_TEST_ROOT}/src/ui/home
    ${QSAN_TEST_ROOT}/src/ui/testing
    ${QSAN_TEST_ROOT}/src/util
)
qsan_add_ctest(qsanguosha_runtime_paths qsanguosha_core_tests
    SUITE runtime-paths
    LABELS "packaging;fast"
)
qsan_add_ctest(qsanguosha_replay_game_state_protocol qsanguosha_core_tests
    SUITE replay-game-state
    LABELS "protocol;fast"
)
qsan_add_ctest(qsanguosha_takeover_snapshot_contract qsanguosha_core_tests
    SUITE takeover-snapshot
    LABELS "replay;server;fast"
    TIMEOUT 60
)
qsan_add_ctest(qsanguosha_photo_layout_fit qsanguosha_core_tests
    SUITE photo-layout-fit
    LABELS "ui;fast"
)
qsan_add_ctest(qsanguosha_engine_smoke qsanguosha_core_tests
    SUITE engine-smoke
    LABELS fast
)
qsan_add_ctest(qsanguosha_equips_nullified qsanguosha_core_tests
    SUITE equips-nullified
    LABELS "gameplay;fast"
)
qsan_add_ctest(qsanguosha_enum_reflection qsanguosha_core_tests
    SUITE enum-reflection
    LABELS "qt6;fast"
)
qsan_add_ctest(qsanguosha_user_name qsanguosha_core_tests
    SUITE user-name
    LABELS "client;fast"
)

# glibc floor:喺新發行版 link 會靜靜綁到最新嘅 symbol version(glibc 2.43 重新
# version 咗 acosf／atan2f／sqrtf),出嚟嘅 binary 喺 Ubuntu 24.04 開唔到。
# src/util/linux-glibc-compat.c 釘住已知嗰幾個;呢個 test 捉下一個。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(QSAN_GLIBC_FLOOR "2.38" CACHE STRING "Newest glibc symbol version a Linux executable may need")
    set(qsan_glibc_floor_binaries "")
    foreach(qsan_glibc_target QSanguosha qsanguosha_server qsanguosha_tui)
        if(TARGET ${qsan_glibc_target})
            list(APPEND qsan_glibc_floor_binaries "$<TARGET_FILE:${qsan_glibc_target}>")
        endif()
    endforeach()
    if(qsan_glibc_floor_binaries)
        add_test(NAME qsanguosha_glibc_floor
            COMMAND ${Python3_EXECUTABLE}
                ${QSAN_TEST_ROOT}/tools/packaging/check-glibc-floor.py
                --max ${QSAN_GLIBC_FLOOR} ${qsan_glibc_floor_binaries}
        )
        set_tests_properties(qsanguosha_glibc_floor PROPERTIES LABELS "packaging;fast")
    endif()
endif()

add_test(NAME qsanguosha_qt6_api_debt
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/qt6-api-debt-check.py"
        --root "${QSAN_TEST_ROOT}"
)
set_tests_properties(qsanguosha_qt6_api_debt PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "qt6;fast"
)

add_library(qsanguosha_protocol_v2_contract_support STATIC
    ${QSAN_TEST_ROOT}/src/core/json.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/gameplay/multiple-choice-payload.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/gameplay/protocol-gameplay-payload-registry.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/gameplay/simple-choice-payloads.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/gameplay/typed-interaction-payloads.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/card-provenance-message.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/protocol-message.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/protocol-runtime.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/protocol-payload-registry.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/protocol-v2-codec.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/session/session-payloads.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/session/client-session-controller.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/skill-instance-message.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/state/player-ui-state.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/switch-context-message.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/sync-pile-message.cpp
)
target_include_directories(qsanguosha_protocol_v2_contract_support PUBLIC
    ${QSAN_TEST_ROOT}/src/core
)
target_precompile_headers(qsanguosha_protocol_v2_contract_support PRIVATE
    ${QSAN_TEST_ROOT}/src/core/engine-pch.h
)
target_link_libraries(qsanguosha_protocol_v2_contract_support PUBLIC
    Qt6::Core
    Qt6::Network
)

# All protocol-layer contracts live in one executable: the five suites already
# share the same link surface (the support lib already builds the
# card-provenance/skill-instance/switch-context/sync-pile/player-ui-state
# sources, so protocol-messages does not compile them again); merging just
# removes four Visual Studio projects. Each suite still runs in its own process.
add_executable(qsanguosha_protocol_tests
    protocol-tests-main.cpp
    protocol-messages-test.cpp
    protocol/protocol-v2-codec-test.cpp
    protocol/protocol-flow-inventory-test.cpp
    protocol/all-interaction-payloads-test.cpp
    client_core/interaction-reply-adapter-test.cpp
    ${QSAN_TEST_ROOT}/src/client/interaction-reply-encoder.cpp
    ${QSAN_TEST_ROOT}/src/client/interaction-reply-coordinator.cpp
)
set_source_files_properties(protocol-messages-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runProtocolMessagesTests)
set_source_files_properties(protocol/protocol-v2-codec-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runProtocolV2CodecTests)
set_source_files_properties(protocol/protocol-flow-inventory-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runProtocolFlowInventoryTests)
set_source_files_properties(protocol/all-interaction-payloads-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runAllInteractionPayloadTests)
set_source_files_properties(client_core/interaction-reply-adapter-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runInteractionReplyAdapterTests)
target_include_directories(qsanguosha_protocol_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/client
    ${QSAN_TEST_ROOT}/src/core
)
target_link_libraries(qsanguosha_protocol_tests PRIVATE
    qsanguosha_client_core
    qsanguosha_protocol_v2_contract_support
    Qt6::Core
)
qsan_add_ctest(qsanguosha_protocol_messages qsanguosha_protocol_tests
    SUITE protocol-messages
    LABELS protocol
)
qsan_add_ctest(qsanguosha_protocol_v2_contract qsanguosha_protocol_tests
    SUITE v2-codec
    LABELS "protocol;fast"
)
qsan_add_ctest(qsanguosha_protocol_flow_inventory_contract qsanguosha_protocol_tests
    SUITE flow-inventory
    LABELS "protocol;fast"
)
qsan_add_ctest(qsanguosha_protocol_all_interaction_payload_contract
    qsanguosha_protocol_tests
    SUITE all-interaction-payloads
    LABELS "protocol;fast"
)
qsan_add_ctest(qsanguosha_interaction_reply_adapter_contract qsanguosha_protocol_tests
    SUITE reply-adapter
    LABELS "client-core;protocol;fast"
)

add_test(NAME qsanguosha_card_overview_contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-test-suite.py
        --suite CARD_OVERVIEW_CONTRACT_RESULT
        --timeout 120
        --working-directory ${QSAN_TEST_ROOT}
        --case "classifier::$<TARGET_FILE:qsanguosha_core_tests>::--suite::card-overview-classifier"
        --case "model::$<TARGET_FILE:qsanguosha_core_tests>::--suite::card-overview-model"
)
set_tests_properties(qsanguosha_card_overview_contract PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "ui;fast"
)
if(WIN32)
    set_property(TEST qsanguosha_card_overview_contract PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

# Contracts for the Linux GUI M1/M2/M2B-A/M2B-B markers, exit codes, command
# mapping, and effects profiles. All four link only Qt6::Core: the contracts
# must not require QApplication, Qt Multimedia, OpenGL, or art assets to be
# verified, so a server-only configure works too. Merging into one target just
# removes three Visual Studio projects; each suite still runs in its own
# process (see tests/ui-contract-tests-main.cpp).
# AUTOMOC is required because the effects test contains a minimal animation
# target with a Q_PROPERTY.
add_executable(qsanguosha_ui_contract_tests
    ui-contract-tests-main.cpp
    ui_startup_smoke/ui-startup-smoke-report-test.cpp
    network_ui_smoke/network-ui-smoke-report-test.cpp
    multimedia_smoke/multimedia-smoke-report-test.cpp
    effects_profile/effects-profile-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/ui-startup-smoke-report.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/ui-startup-smoke-report.h
    ${QSAN_TEST_ROOT}/src/ui/testing/network-ui-smoke-report.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/network-ui-smoke-report.h
    ${QSAN_TEST_ROOT}/src/ui/testing/multimedia-smoke-report.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/multimedia-smoke-report.h
    ${QSAN_TEST_ROOT}/src/ui/effects/effects-completion.cpp
    ${QSAN_TEST_ROOT}/src/ui/effects/effects-completion.h
    ${QSAN_TEST_ROOT}/src/ui/effects/effects-profile.cpp
    ${QSAN_TEST_ROOT}/src/ui/effects/effects-profile.h
)
set_source_files_properties(ui_startup_smoke/ui-startup-smoke-report-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runUiStartupSmokeReportTests)
set_source_files_properties(network_ui_smoke/network-ui-smoke-report-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runNetworkUiSmokeReportTests)
set_source_files_properties(multimedia_smoke/multimedia-smoke-report-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runMultimediaSmokeReportTests)
set_source_files_properties(effects_profile/effects-profile-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runEffectsProfileTests)
set_target_properties(qsanguosha_ui_contract_tests PROPERTIES AUTOMOC ON)
target_include_directories(qsanguosha_ui_contract_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/core
    ${QSAN_TEST_ROOT}/src/ui/effects
    ${QSAN_TEST_ROOT}/src/ui/testing
)
target_link_libraries(qsanguosha_ui_contract_tests PRIVATE Qt6::Core)
qsan_add_ctest(qsanguosha_ui_contract qsanguosha_ui_contract_tests
    LABELS "ui;fast"
)
qsan_add_ctest(qsanguosha_effects_profile_contract qsanguosha_ui_contract_tests
    SUITE effects-profile
    LABELS "ui;fast"
)

# Client Architecture F1: the ClientCore contract. It links only Qt6::Core and
# qsanguosha_client_core -- if this test ever needed the GUI to link, that
# would mean ClientCore had leaked a GUI dependency in, so the test itself is
# the gate for the "ClientCore does not depend on the GUI" acceptance rule.
# A server-only configure works too.
add_executable(qsanguosha_client_core_tests
    client_core/client-core-test.cpp
)
target_link_libraries(qsanguosha_client_core_tests PRIVATE
    qsanguosha_client_core
    Qt6::Core
)
set(qsan_client_core_contract_arguments)
if(TARGET QSanguosha)
    list(APPEND qsan_client_core_contract_arguments
        --interaction-generator $<TARGET_FILE:QSanguosha>
        --interaction-matrix ${QSAN_TEST_ROOT}/artifacts/client-core-interaction-matrix.json
        --generated-interaction-matrix ${CMAKE_CURRENT_BINARY_DIR}/generated-client-core-interaction-matrix.json
    )
endif()
qsan_add_ctest(qsanguosha_client_core_contract qsanguosha_client_core_tests
    LABELS "client-core;fast"
    ARGS ${qsan_client_core_contract_arguments}
    TIMEOUT 120
)

# Compile the public runtime surface using only its own usage requirements.
# No engine/TUI target may mask missing includes or compile definitions here.
# Keep this check enabled when the TUI product is disabled as well.
add_library(qsanguosha_client_runtime_headers_test OBJECT
    client_core/client-runtime-headers-test.cpp
)
target_link_libraries(qsanguosha_client_runtime_headers_test PRIVATE
    qsanguosha_client_runtime
)

if(QSAN_BUILD_TUI)
    # The four TUI tests live in one executable: their link surface is already
    # the same superset (live-tcp already links both the engine and protocol v2
    # support), so merging just removes three Visual Studio projects. Each
    # suite still runs in its own process and no test file changed a line --
    # main() is renamed via COMPILE_DEFINITIONS.
    add_executable(qsanguosha_tui_tests
        tui-tests-main.cpp
        tui/tui-board-layout-test.cpp
        tui/tui-board-presenter-test.cpp
        tui/tui-board-view-test.cpp
        tui/tui-card-text-test.cpp
        tui/tui-client-player-test.cpp
        tui/tui-log-text-test.cpp
        tui/tui-play-skills-test.cpp
        tui/tui-target-advice-test.cpp
        tui/tui-contract-test.cpp
        tui/tui-live-tcp-test.cpp
        tui/tui-presenter-test.cpp
        tui/tui-text-width-test.cpp
        tui/tui-screen-test.cpp
        tui/tui-terminal-test.cpp
        tui/tui-line-editor-test.cpp
        tui/tui-ui-mode-test.cpp
        tui/tui-ui-parity-test.cpp
        ${QSAN_TEST_ROOT}/src/client/client-live-session.cpp
        ${QSAN_TEST_ROOT}/src/client/interaction-command-registry.cpp
        ${QSAN_TEST_ROOT}/src/client/interaction-reply-coordinator.cpp
        ${QSAN_TEST_ROOT}/src/client/interaction-reply-encoder.cpp
        ${QSAN_TEST_ROOT}/src/client/interaction-request-factory.cpp
        ${QSAN_TEST_ROOT}/src/client/protocol-interaction-request-builder.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-board-layout.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-board-presenter.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-board-view.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-client-player.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-command.cpp
        # Keep TuiInput's moc in the same target as the other directly built
        # TUI objects; pulling the support library's combined moc duplicates them.
        ${QSAN_TEST_ROOT}/src/tui/tui-input.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-interaction-view.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-renderer.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-script-runner.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-target-advice.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-terminal.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-line-editor.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-ui-mode.cpp
    )
    set_source_files_properties(tui/tui-board-layout-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiBoardLayoutTests)
    set_source_files_properties(tui/tui-board-presenter-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiBoardPresenterTests)
    set_source_files_properties(tui/tui-board-view-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiBoardViewTests)
    set_source_files_properties(tui/tui-card-text-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiCardTextTests)
    set_source_files_properties(tui/tui-client-player-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiClientPlayerTests)
    set_source_files_properties(tui/tui-log-text-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiLogTextTests)
    set_source_files_properties(tui/tui-play-skills-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiPlaySkillsTests)
    set_source_files_properties(tui/tui-target-advice-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiTargetAdviceTests)
    set_source_files_properties(tui/tui-contract-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiContractTests)
    set_source_files_properties(tui/tui-live-tcp-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiLiveTcpTests)
    set_source_files_properties(tui/tui-presenter-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiPresenterTests)
    set_source_files_properties(tui/tui-text-width-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiTextWidthTests)
    set_source_files_properties(tui/tui-screen-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiScreenTests)
    set_source_files_properties(tui/tui-terminal-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiTerminalTests)
    set_source_files_properties(tui/tui-line-editor-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiLineEditorTests)
    set_source_files_properties(tui/tui-ui-mode-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiUiModeTests)
    set_source_files_properties(tui/tui-ui-parity-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiUiParityTests)
    target_include_directories(qsanguosha_tui_tests PRIVATE
        ${QSAN_TEST_ROOT}/src/client
        ${QSAN_TEST_ROOT}/src/client/core
        ${QSAN_TEST_ROOT}/src/core
        ${QSAN_TEST_ROOT}/src/tui
        ${QSAN_TEST_ROOT}/src/util
    )
    target_compile_definitions(qsanguosha_tui_tests PRIVATE
        QSAN_ENGINE_TEST_BUILD
        QSAN_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(qsanguosha_tui_tests PRIVATE
        qsanguosha_client_core
        qsanguosha_tui_support
        qsanguosha_engine
        qsanguosha_protocol_v2_contract_support
        Qt6::Core
        Qt6::Network
    )
    qsan_add_ctest(qsanguosha_tui_board_layout qsanguosha_tui_tests
        SUITE board-layout
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_board_presenter qsanguosha_tui_tests
        SUITE board-presenter
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_board_view qsanguosha_tui_tests
        SUITE board-view
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_card_text qsanguosha_tui_tests
        SUITE card-text
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_client_player qsanguosha_tui_tests
        SUITE client-player
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_log_text qsanguosha_tui_tests
        SUITE log-text
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_play_skills qsanguosha_tui_tests
        SUITE play-skills
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_target_advice qsanguosha_tui_tests
        SUITE target-advice
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_contract qsanguosha_tui_tests
        SUITE contract
        LABELS "client;tui;protocol;fast"
        TIMEOUT 30
    )
    qsan_add_ctest(qsanguosha_tui_live_tcp qsanguosha_tui_tests
        SUITE live-tcp
        LABELS "client;tui;protocol;network;fast"
        TIMEOUT 15
    )
    qsan_add_ctest(qsanguosha_tui_presenter qsanguosha_tui_tests
        SUITE presenter
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_text_width qsanguosha_tui_tests
        SUITE text-width
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_screen qsanguosha_tui_tests
        SUITE screen
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_terminal qsanguosha_tui_tests
        SUITE terminal
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_line_editor qsanguosha_tui_tests
        SUITE line-editor
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_ui_mode qsanguosha_tui_tests
        SUITE ui-mode
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_ui_parity qsanguosha_tui_tests
        SUITE ui-parity
        LABELS "client;tui;protocol;fast"
        TIMEOUT 180
    )
endif()

# M3: the packaging contract. This test only reads the repository and never
# builds a package, so CI can always run it -- real packaging and the artifact
# smoke checks live in linux-package-ci.yml.
add_test(NAME qsanguosha_packaging_contract
    COMMAND ${Python3_EXECUTABLE}
        ${QSAN_TEST_ROOT}/tools/autotest/tests/test_packaging_contract.py
)
set_tests_properties(qsanguosha_packaging_contract PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "packaging;fast"
)

# 執行期 Lua 內容係由另一個 repository fetch 返嚟嘅:漏咗 subdirectory(extensions/temp、
# ai/temp)唔會即刻報錯,要等到 engine bootstrap 先炸。呢個 test 用 fixture repository 驗 fetch 本身。
add_test(NAME qsanguosha_fetch_extensions_contract
    COMMAND ${Python3_EXECUTABLE}
        ${QSAN_TEST_ROOT}/tools/autotest/tests/test_fetch_extensions_contract.py
)
set_tests_properties(qsanguosha_fetch_extensions_contract PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "packaging;fast"
    TIMEOUT 120
)

add_test(NAME qsanguosha_ui_runner_contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-test-suite.py
        --suite UI_RUNNER_CONTRACT_RESULT
        --timeout 300
        --working-directory ${QSAN_TEST_ROOT}
        --case "local-response-case-parser::$<TARGET_FILE:qsanguosha_core_tests>::--suite::local-response-case-parser"
        --case "startup-smoke-cli::${Python3_EXECUTABLE}::${QSAN_TEST_ROOT}/tools/autotest/tests/test_ui_startup_smoke_contract.py"
        --case "network-smoke-cli::${Python3_EXECUTABLE}::${QSAN_TEST_ROOT}/tools/autotest/tests/test_gui_network_smoke_contract.py"
        --case "skill-ui-runner-cli::${Python3_EXECUTABLE}::${QSAN_TEST_ROOT}/tools/autotest/tests/test_skill_ui_runner_contract.py"
)
set_tests_properties(qsanguosha_ui_runner_contract PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "ui;fast"
)
if(WIN32)
    set_property(TEST qsanguosha_ui_runner_contract PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

# The tests around the dedicated server (command line, logger, real TCP
# integration) live in one executable: none of the three links the engine,
# only Qt6::Core/Qt6::Network plus protocol v2 support, so merging changes no
# suite's link surface and just removes two Visual Studio projects.
add_executable(qsanguosha_server_support_tests
    server-support-tests-main.cpp
    server-command-line-test.cpp
    server-logger-test.cpp
    server-network-integration-test.cpp
    ${QSAN_TEST_ROOT}/src/server/server-command-line.cpp
    ${QSAN_TEST_ROOT}/src/server/server-command-line.h
    ${QSAN_TEST_ROOT}/src/server/server-config.cpp
    ${QSAN_TEST_ROOT}/src/server/server-config.h
    ${QSAN_TEST_ROOT}/src/server/server-logger.cpp
    ${QSAN_TEST_ROOT}/src/server/server-logger.h
)
set_source_files_properties(server-command-line-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runServerCommandLineTests)
set_source_files_properties(server-logger-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runServerLoggerTests)
set_source_files_properties(server-network-integration-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runServerNetworkIntegrationTests)
target_include_directories(qsanguosha_server_support_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/core
    ${QSAN_TEST_ROOT}/src/server
)
target_link_libraries(qsanguosha_server_support_tests PRIVATE
    qsanguosha_protocol_v2_contract_support
    Qt6::Core
    Qt6::Network
)
qsan_add_ctest(qsanguosha_server_cli_contract qsanguosha_server_support_tests
    SUITE server-cli
    LABELS "server;fast"
    ARGS
        --server $<TARGET_FILE:qsanguosha_server>
        --config ${QSAN_TEST_ROOT}/docs/server.ini.example
    TIMEOUT 180
)
qsan_add_ctest(qsanguosha_server_logging qsanguosha_server_support_tests
    SUITE server-logging
    LABELS "server;fast"
)

if(QSAN_ENABLE_WEBSOCKETS)
    add_executable(qsanguosha_websocket_gateway_tests
        websocket-gateway-test.cpp
    )
    target_include_directories(qsanguosha_websocket_gateway_tests PRIVATE
        ${QSAN_TEST_ROOT}/src/core
    )
    target_link_libraries(qsanguosha_websocket_gateway_tests PRIVATE
        qsanguosha_protocol_v2_contract_support
        Qt6::WebSockets
    )
    qsan_add_ctest(qsanguosha_websocket_gateway_contract qsanguosha_websocket_gateway_tests
        LABELS "server;fast"
        ARGS --server $<TARGET_FILE:qsanguosha_server>
        TIMEOUT 60
    )
endif()
if(UNIX)
    add_test(NAME qsanguosha_systemd_unit
        COMMAND "${Python3_EXECUTABLE}"
            "${QSAN_TEST_ROOT}/tests/systemd-unit-test.py"
            --service "${QSAN_SYSTEMD_SERVICE_FILE}"
    )
    set_tests_properties(qsanguosha_systemd_unit PROPERTIES
        WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
        LABELS "packaging;server;fast"
    )
endif()
if(UNIX)
    add_test(NAME qsanguosha_server_console
        COMMAND bash ${QSAN_TEST_ROOT}/tools/ci/server-console-smoke.sh
            $<TARGET_FILE:qsanguosha_server>
            ${CMAKE_CURRENT_BINARY_DIR}/server-console-smoke.log
    )
    set_tests_properties(qsanguosha_server_console PROPERTIES
        WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
        LABELS server
        TIMEOUT 45
    )
    add_test(NAME qsanguosha_server_logging_smoke
        COMMAND bash ${QSAN_TEST_ROOT}/tools/ci/server-logging-smoke.sh
            $<TARGET_FILE:qsanguosha_server>
            ${CMAKE_CURRENT_BINARY_DIR}/server-logging-smoke.log
    )
    set_tests_properties(qsanguosha_server_logging_smoke PROPERTIES
        WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
        LABELS server
        TIMEOUT 45
    )
endif()

if(UNIX)
    add_test(NAME qsanguosha_network_integration
        COMMAND qsanguosha_server_support_tests
            --suite network-integration
            --server $<TARGET_FILE:qsanguosha_server>
    )
    set_tests_properties(qsanguosha_network_integration PROPERTIES
        WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
        LABELS network
        RUN_SERIAL TRUE
        TIMEOUT 1200
    )
endif()

add_executable(qsanguosha_runtime_tests
    runtime-tests-main.cpp
    card-lifetime-manager-test.cpp
    card-lifetime-event-lease/card-lifetime-event-lease-test.cpp
    card-lifetime-wrapped-adoption/wrapped-adoption-test.cpp
    card-lifetime-shutdown/shutdown_protocol_test.cpp
    lua-runtime-isolation-test.cpp
    room-runtime-isolation-test.cpp
    room-runtime-lua-teardown-test.cpp
)
set_source_files_properties(
    card-lifetime-event-lease/card-lifetime-event-lease-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runCardLifetimeEventLeaseFixture)
set_source_files_properties(
    card-lifetime-wrapped-adoption/wrapped-adoption-test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runCardLifetimeWrappedAdoptionFixture)
set_source_files_properties(
    card-lifetime-shutdown/shutdown_protocol_test.cpp
    PROPERTIES COMPILE_DEFINITIONS main=runCardLifetimeShutdownFixture)
qsan_link_engine_test(qsanguosha_runtime_tests)
target_compile_definitions(qsanguosha_runtime_tests PRIVATE
    QSAN_GENERATED_WRAPPER_PATH="${qsan_swig_wrapper}"
)
qsan_add_ctest(qsanguosha_runtime_contract qsanguosha_runtime_tests
    SUITE runtime-contract
    LABELS runtime
    ARGS --seed 2026082201
    TIMEOUT 1200
)
add_test(NAME qsanguosha_card_lifetime_source_check
    COMMAND "${Python3_EXECUTABLE}" "${QSAN_TEST_ROOT}/tools/check-card-lifetime.py"
            --root "${QSAN_TEST_ROOT}"
            --ledger docs/card-lifetime-ownership.md
            --allowlist tools/card-lifetime-allowlist.json)
set_tests_properties(qsanguosha_card_lifetime_source_check PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}" LABELS runtime)
add_test(NAME qsanguosha_card_lifetime_stress
    COMMAND qsanguosha_runtime_tests --suite card-lifetime-synthetic-50 --seed 2026082201)
set_tests_properties(qsanguosha_card_lifetime_stress PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}" LABELS "runtime;stress" TIMEOUT 300)
if(WIN32)
    set_property(TEST qsanguosha_card_lifetime_stress PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

add_executable(qsanguosha_server_tests
    server-tests-main.cpp
    room-notifier-test.cpp
    skill-runtime-coordinator-test.cpp
    request-coordinator-test.cpp
    pre-game-lock-test.cpp
    outbound-ordering-test.cpp
    room-disconnect-test.cpp
    move-classifier-ordering/move-classifier-ordering-test.cpp
    extra-turn-scheduler-test.cpp
    game-session-controller-test.cpp
    room-roster-test.cpp
    player-lifecycle-service-test.cpp
    player-decision-service-test.cpp
    game-snapshot-tags-test.cpp
    roomthread-deferred-state-test.cpp
)
qsan_link_engine_test(qsanguosha_server_tests)
qsan_add_ctest(qsanguosha_server_unit qsanguosha_server_tests
    LABELS server
    TIMEOUT 1800
)

add_executable(qsanguosha_roomthread_perf_tests
    roomthread-perf-main.cpp
    roomthread-deferred-state-test.cpp
)
qsan_link_engine_test(qsanguosha_roomthread_perf_tests)
qsan_add_ctest(qsanguosha_roomthread_perf qsanguosha_roomthread_perf_tests
    LABELS "server;fast"
    TIMEOUT 30
)

add_executable(qsanguosha_replay_v2_tests
    replay-v2/replay-v2-test.cpp
    ${QSAN_TEST_ROOT}/src/core/replay/replay-codec.cpp
    ${QSAN_TEST_ROOT}/src/core/replay/replay-container.cpp
)
target_include_directories(qsanguosha_replay_v2_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/core
)
target_link_libraries(qsanguosha_replay_v2_tests PRIVATE
    qsanguosha_protocol_v2_contract_support
    Qt6::Gui
)
qsan_add_ctest(qsanguosha_replay_v2_contract qsanguosha_replay_v2_tests
    LABELS "replay;protocol;fast"
)

# qsanguosha_takeover_snapshot_tests and qsanguosha_ui_tests used to live here. They
# duplicated the sources and the CTest names that bf1f85d had already folded into
# qsanguosha_core_tests; the merge 7fdf0c6 kept both sides, so add_test() failed with
# "test NAME ... already exists" and the project would not configure at all.

# Small protocol identity gate: runs without Engine, a Room, or network timers.
add_executable(qsanguosha_rules_identity_tests
    protocol/rules-bundle-identity-test.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/rules-bundle-identity.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/session/session-payloads.cpp)
target_include_directories(qsanguosha_rules_identity_tests PRIVATE ${QSAN_TEST_ROOT}/src/core)
target_link_libraries(qsanguosha_rules_identity_tests PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(qsanguosha_rules_identity_tests PRIVATE /utf-8)
endif()
add_test(NAME qsanguosha_rules_identity COMMAND qsanguosha_rules_identity_tests)
set_tests_properties(qsanguosha_rules_identity PROPERTIES LABELS "protocol;fast" TIMEOUT 15)

add_test(NAME qsanguosha_rules_bundle_harness
    COMMAND "${Python3_EXECUTABLE}" "${QSAN_TEST_ROOT}/tests/client_runtime/check-rules-bundle.py" --self-test)
set_tests_properties(qsanguosha_rules_bundle_harness PROPERTIES LABELS "client;protocol;fast" TIMEOUT 15)

# Pure-data manifest contract: no Engine, Lua state, or filesystem.
add_executable(qsanguosha_rules_content_manifest_tests
    protocol/rules-content-manifest-test.cpp
    ${QSAN_TEST_ROOT}/src/core/rules-content-manifest.cpp)
target_include_directories(qsanguosha_rules_content_manifest_tests PRIVATE ${QSAN_TEST_ROOT}/src/core)
target_link_libraries(qsanguosha_rules_content_manifest_tests PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(qsanguosha_rules_content_manifest_tests PRIVATE /utf-8)
endif()
add_test(NAME qsanguosha_rules_content_manifest COMMAND qsanguosha_rules_content_manifest_tests)
set_tests_properties(qsanguosha_rules_content_manifest PROPERTIES LABELS "protocol;fast" TIMEOUT 15)

add_test(NAME qsanguosha_extension_manifest
    COMMAND "${Python3_EXECUTABLE}" "${QSAN_TEST_ROOT}/tests/client_runtime/check-extension-manifest.py"
            --root "${QSAN_TEST_ROOT}")
set_tests_properties(qsanguosha_extension_manifest PROPERTIES LABELS "client;protocol;fast" TIMEOUT 15)
add_test(NAME qsanguosha_extension_manifest_selftest
    COMMAND "${Python3_EXECUTABLE}" "${QSAN_TEST_ROOT}/tests/client_runtime/check-extension-manifest.py"
            --self-test)
set_tests_properties(qsanguosha_extension_manifest_selftest PROPERTIES LABELS "client;protocol;fast" TIMEOUT 15)

if(TARGET qsanguosha_rules_fixture_runner)
    add_test(NAME qsanguosha_rules_manifest_native
        COMMAND "${Python3_EXECUTABLE}" "${QSAN_TEST_ROOT}/tests/client_runtime/check-rules-bundle.py"
                --native-only --native-runner "$<TARGET_FILE:qsanguosha_rules_fixture_runner>"
                --artifacts "${CMAKE_CURRENT_BINARY_DIR}/rules-manifest-native")
    set_tests_properties(qsanguosha_rules_manifest_native PROPERTIES LABELS "client;protocol" TIMEOUT 180)
endif()
