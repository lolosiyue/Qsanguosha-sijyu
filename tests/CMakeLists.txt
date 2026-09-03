set(QSAN_TEST_ROOT "${CMAKE_SOURCE_DIR}")
# 呢個目錄入面起嘅 target 全部收落 Visual Studio 嘅 "Tests" solution folder。
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

# 所有「link engine + Qt6::Gui」嘅測試住喺同一個執行檔。佢哋本身嘅 link 面
# 完全一樣(qsan_link_engine_test),分開只係多七個 Visual Studio project。
# 每個 suite 依然行喺自己嘅 process:夾埋入嚟嗰啲仍然係原本嗰個 main(),
# 靠 COMPILE_DEFINITIONS main=... 改名,測試檔本身一行都冇改過。
add_executable(qsanguosha_core_tests
    core-tests-main.cpp
    engine-smoke-test.cpp
    lua-compat-test.cpp
    core-regressions-test.cpp
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
qsan_add_ctest(qsanguosha_enum_reflection qsanguosha_core_tests
    SUITE enum-reflection
    LABELS "qt6;fast"
)

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

# 協定層契約全部住喺同一個執行檔:五個 suite 嘅 link 面本身就一樣(support lib
# 已經有 card-provenance／skill-instance／switch-context／sync-pile／player-ui-state
# 呢幾份 source,所以 protocol-messages 唔使再自己編一份),夾埋只係少咗四個
# Visual Studio project。每個 suite 依然行喺自己嘅 process。
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

# Linux GUI M1／M2／M2B-A／M2B-B 嘅 marker、exit code、command 對照同效果
# profile 契約。四個都只 link Qt6::Core:契約唔應該要開 QApplication、Qt
# Multimedia、OpenGL 或者美術資產先驗到,所以 server-only configure 一樣跑得。
# 夾埋一個 target 只係少咗三個 Visual Studio project,每個 suite 依然行喺
# 自己嘅 process(見 tests/ui-contract-tests-main.cpp)。
# AUTOMOC 要開,因為 effects 測試入面有一個帶 Q_PROPERTY 嘅最小動畫目標。
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

# Client Architecture F1:ClientCore 契約。只 link Qt6::Core 同
# qsanguosha_client_core —— 呢個 test 一旦要拉 GUI 先 link 得到,就代表
# ClientCore 已經漏咗 GUI 依賴入去,所以佢本身就係「ClientCore 唔依賴 GUI」
# 呢條完成標準嘅 gate。server-only configure 一樣跑得。
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

if(QSAN_BUILD_TUI)
    # 四個 TUI 測試住喺同一個執行檔:佢哋嘅 link 面本身係同一個超集
    # (live-tcp 已經同時 link engine 同 protocol v2 support),夾埋只係少咗
    # 三個 Visual Studio project。每個 suite 依然行喺自己嘅 process,測試檔
    # 本身一行都冇改過 —— main() 靠 COMPILE_DEFINITIONS 改名。
    add_executable(qsanguosha_tui_tests
        tui-tests-main.cpp
        tui/tui-card-text-test.cpp
        tui/tui-log-text-test.cpp
        tui/tui-contract-test.cpp
        tui/tui-live-tcp-test.cpp
        ${QSAN_TEST_ROOT}/src/client/client-live-session.cpp
        ${QSAN_TEST_ROOT}/src/client/interaction-command-registry.cpp
        ${QSAN_TEST_ROOT}/src/client/interaction-reply-coordinator.cpp
        ${QSAN_TEST_ROOT}/src/client/interaction-reply-encoder.cpp
        ${QSAN_TEST_ROOT}/src/client/interaction-request-factory.cpp
        ${QSAN_TEST_ROOT}/src/client/protocol-interaction-request-builder.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-command.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-interaction-view.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-renderer.cpp
        ${QSAN_TEST_ROOT}/src/tui/tui-script-runner.cpp
    )
    set_source_files_properties(tui/tui-card-text-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiCardTextTests)
    set_source_files_properties(tui/tui-log-text-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiLogTextTests)
    set_source_files_properties(tui/tui-contract-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiContractTests)
    set_source_files_properties(tui/tui-live-tcp-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiLiveTcpTests)
    target_include_directories(qsanguosha_tui_tests PRIVATE
        ${QSAN_TEST_ROOT}/src/client
        ${QSAN_TEST_ROOT}/src/client/core
        ${QSAN_TEST_ROOT}/src/core
        ${QSAN_TEST_ROOT}/src/tui
        ${QSAN_TEST_ROOT}/src/util
    )
    target_compile_definitions(qsanguosha_tui_tests PRIVATE
        QSAN_ENGINE_TEST_BUILD
    )
    target_link_libraries(qsanguosha_tui_tests PRIVATE
        qsanguosha_client_core
        qsanguosha_tui_support
        qsanguosha_engine
        qsanguosha_protocol_v2_contract_support
        Qt6::Core
        Qt6::Network
    )
    qsan_add_ctest(qsanguosha_tui_card_text qsanguosha_tui_tests
        SUITE card-text
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
    qsan_add_ctest(qsanguosha_tui_log_text qsanguosha_tui_tests
        SUITE log-text
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
endif()

# M3:打包契約。呢個 test 只讀 repository,唔會整包,所以每次 CI 都跑得起 ——
# 真正整包同由成品 smoke 嗰啲喺 linux-package-ci.yml。
add_test(NAME qsanguosha_packaging_contract
    COMMAND ${Python3_EXECUTABLE}
        ${QSAN_TEST_ROOT}/tools/autotest/tests/test_packaging_contract.py
)
set_tests_properties(qsanguosha_packaging_contract PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "packaging;fast"
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

# dedicated server 周邊嘅測試(command line、logger、真 TCP 整合)住喺同一個
# 執行檔:三個都唔 link engine,淨係 Qt6::Core／Qt6::Network 加 protocol v2
# support,夾埋唔會改變任何一個嘅 link 面,只係少咗兩個 Visual Studio project。
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
