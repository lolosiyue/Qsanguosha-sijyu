set(QSAN_TEST_ROOT "${CMAKE_SOURCE_DIR}")
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

add_executable(qsanguosha_core_tests
    core-tests-main.cpp
    engine-smoke-test.cpp
    core-regressions-test.cpp
    ${QSAN_TEST_ROOT}/src/server/server-core.h
)
qsan_link_engine_test(qsanguosha_core_tests)
qsan_add_ctest(qsanguosha_engine_smoke qsanguosha_core_tests
    SUITE engine-smoke
    LABELS fast
)

add_executable(qsanguosha_protocol_tests
    protocol-messages-test.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/card-provenance-message.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/skill-instance-message.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/switch-context-message.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/sync-pile-message.cpp
    ${QSAN_TEST_ROOT}/src/core/protocol/state/player-ui-state.cpp
)
target_include_directories(qsanguosha_protocol_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/core
)
target_link_libraries(qsanguosha_protocol_tests PRIVATE Qt6::Core)
qsan_add_ctest(qsanguosha_protocol_messages qsanguosha_protocol_tests
    LABELS protocol
)

add_executable(qsanguosha_protocol_v1_tests
    protocol/protocol-v1-codec-test.cpp
)
target_include_directories(qsanguosha_protocol_v1_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/core
)
target_link_libraries(qsanguosha_protocol_v1_tests PRIVATE
    qsanguosha_engine
    Qt6::Core
)
qsan_add_ctest(qsanguosha_protocol_v1_contract qsanguosha_protocol_v1_tests
    LABELS "protocol;fast"
)

add_executable(qsanguosha_protocol_negotiation_tests
    protocol/protocol-negotiation-test.cpp
)
target_include_directories(qsanguosha_protocol_negotiation_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/core
)
target_link_libraries(qsanguosha_protocol_negotiation_tests PRIVATE
    qsanguosha_engine
    Qt6::Core
)

add_executable(qsanguosha_card_overview_classifier_tests
    card-overview-classifier-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/card-overview-data.cpp
    ${QSAN_TEST_ROOT}/src/ui/card-overview-data.h
    ${QSAN_TEST_ROOT}/src/server/server-core.h
)
target_include_directories(qsanguosha_card_overview_classifier_tests PRIVATE
    ${QSAN_ENGINE_TEST_INCLUDES}
    ${QSAN_TEST_ROOT}/src/ui
)
target_compile_definitions(qsanguosha_card_overview_classifier_tests PRIVATE
    QSAN_ENGINE_TEST_BUILD
)
target_link_libraries(qsanguosha_card_overview_classifier_tests PRIVATE
    qsanguosha_engine
    Qt6::Core
)
add_executable(qsanguosha_card_overview_model_tests
    card-overview-model-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/card-overview-data.cpp
    ${QSAN_TEST_ROOT}/src/ui/card-overview-data.h
    ${QSAN_TEST_ROOT}/src/ui/home/homecardmodel.cpp
    ${QSAN_TEST_ROOT}/src/ui/home/homecardmodel.h
    ${QSAN_TEST_ROOT}/src/server/server-core.h
)
qsan_link_engine_test(qsanguosha_card_overview_model_tests)
target_include_directories(qsanguosha_card_overview_model_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/ui
    ${QSAN_TEST_ROOT}/src/ui/home
)
add_test(NAME qsanguosha_card_overview_contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-test-suite.py
        --suite CARD_OVERVIEW_CONTRACT_RESULT
        --timeout 120
        --working-directory ${QSAN_TEST_ROOT}
        --case "classifier::$<TARGET_FILE:qsanguosha_card_overview_classifier_tests>"
        --case "model::$<TARGET_FILE:qsanguosha_card_overview_model_tests>"
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

add_executable(qsanguosha_local_response_ui_case_tests
    skill_ui_runner/local-response-ui-case-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/local-response-ui-case.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/local-response-ui-case.h
)
qsan_link_engine_test(qsanguosha_local_response_ui_case_tests)
target_include_directories(qsanguosha_local_response_ui_case_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/ui/testing
)
# Linux GUI M1 startup smoke 的 marker／exit code 契約。只 link Qt6::Core，令
# server-only configure 都跑得到，唔會為咗測試而拉 GUI 依賴入 CTest。
add_executable(qsanguosha_ui_startup_smoke_tests
    ui_startup_smoke/ui-startup-smoke-report-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/ui-startup-smoke-report.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/ui-startup-smoke-report.h
)
target_include_directories(qsanguosha_ui_startup_smoke_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/ui/testing
)
target_link_libraries(qsanguosha_ui_startup_smoke_tests PRIVATE Qt6::Core)
# Linux GUI M2 network UI smoke 的 marker／exit code／command 對照契約。同 M1
# 一樣只 link Qt6::Core:契約唔應該要開 QApplication 或者起一局真網絡局先驗到。
add_executable(qsanguosha_network_ui_smoke_tests
    network_ui_smoke/network-ui-smoke-report-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/network-ui-smoke-report.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/network-ui-smoke-report.h
)
target_include_directories(qsanguosha_network_ui_smoke_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/core
    ${QSAN_TEST_ROOT}/src/ui/testing
)
target_link_libraries(qsanguosha_network_ui_smoke_tests PRIVATE Qt6::Core)
# Linux GUI M2B-A multimedia smoke 的 marker／exit code／影片分類契約。同 M1／M2
# 一樣只 link Qt6::Core：契約唔應該要開 QApplication、Qt Multimedia 或者真音訊
# 裝置先驗到。
add_executable(qsanguosha_multimedia_smoke_tests
    multimedia_smoke/multimedia-smoke-report-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/multimedia-smoke-report.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/multimedia-smoke-report.h
)
target_include_directories(qsanguosha_multimedia_smoke_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/ui/testing
)
target_link_libraries(qsanguosha_multimedia_smoke_tests PRIVATE Qt6::Core)
add_test(NAME qsanguosha_ui_contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-test-suite.py
        --suite UI_CONTRACT_RESULT
        --timeout 120
        --working-directory ${QSAN_TEST_ROOT}
        --case "startup-smoke-report::$<TARGET_FILE:qsanguosha_ui_startup_smoke_tests>"
        --case "network-smoke-report::$<TARGET_FILE:qsanguosha_network_ui_smoke_tests>"
        --case "multimedia-report::$<TARGET_FILE:qsanguosha_multimedia_smoke_tests>"
)
set_tests_properties(qsanguosha_ui_contract PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "ui;fast"
)
if(WIN32)
    set_property(TEST qsanguosha_ui_contract PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

# Linux GUI M2B-B 效果 profile 契約 + exactly-once completion。只 link
# Qt6::Core:profile 解析、feature gate、duration scale 同 completion 保證
# 全部唔應該要開 QApplication、OpenGL 或者美術資產先驗到。AUTOMOC 要開,
# 因為測試入面有一個帶 Q_PROPERTY 嘅最小動畫目標。
add_executable(qsanguosha_effects_profile_tests
    effects_profile/effects-profile-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/effects/effects-completion.cpp
    ${QSAN_TEST_ROOT}/src/ui/effects/effects-completion.h
    ${QSAN_TEST_ROOT}/src/ui/effects/effects-profile.cpp
    ${QSAN_TEST_ROOT}/src/ui/effects/effects-profile.h
)
set_target_properties(qsanguosha_effects_profile_tests PROPERTIES AUTOMOC ON)
target_include_directories(qsanguosha_effects_profile_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/ui/effects
)
target_link_libraries(qsanguosha_effects_profile_tests PRIVATE Qt6::Core)
qsan_add_ctest(qsanguosha_effects_profile_contract qsanguosha_effects_profile_tests
    LABELS "ui;fast"
)
if(WIN32)
    set_property(TEST qsanguosha_effects_profile_contract
        PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

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

add_executable(qsanguosha_interaction_reply_adapter_tests
    client_core/interaction-reply-adapter-test.cpp
    ${QSAN_TEST_ROOT}/src/client/legacy-v1-interaction-reply-adapter.cpp
    ${QSAN_TEST_ROOT}/src/client/interaction-reply-coordinator.cpp
)
target_include_directories(qsanguosha_interaction_reply_adapter_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/client
    ${QSAN_TEST_ROOT}/src/core
)
target_link_libraries(qsanguosha_interaction_reply_adapter_tests PRIVATE
    qsanguosha_client_core
    Qt6::Core
)
qsan_add_ctest(qsanguosha_interaction_reply_adapter_contract
    qsanguosha_interaction_reply_adapter_tests
    LABELS "client-core;protocol;fast"
)

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
        --case "local-response-case-parser::$<TARGET_FILE:qsanguosha_local_response_ui_case_tests>"
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

add_executable(qsanguosha_cli_tests
    server-command-line-test.cpp
    ${QSAN_TEST_ROOT}/src/server/server-command-line.cpp
    ${QSAN_TEST_ROOT}/src/server/server-command-line.h
    ${QSAN_TEST_ROOT}/src/server/server-config.cpp
    ${QSAN_TEST_ROOT}/src/server/server-config.h
)
target_include_directories(qsanguosha_cli_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/server
)
target_link_libraries(qsanguosha_cli_tests PRIVATE Qt6::Core Qt6::Network)
qsan_add_ctest(qsanguosha_server_cli_contract qsanguosha_cli_tests
    LABELS "server;fast"
    ARGS
        --server $<TARGET_FILE:qsanguosha_server>
        --config ${QSAN_TEST_ROOT}/docs/server.ini.example
    TIMEOUT 180
)
# M3:執行期版面同資產清單。呢個 test 唔 link GUI,因為 resolver 同 manifest
# 兩樣都住喺 engine —— dedicated server 一樣要靠佢哋搵到 lua/。
add_executable(qsanguosha_runtime_paths_tests
    runtime-paths-test.cpp
)
qsan_link_engine_test(qsanguosha_runtime_paths_tests)
qsan_add_ctest(qsanguosha_runtime_paths qsanguosha_runtime_paths_tests
    LABELS "packaging;fast"
)

add_executable(qsanguosha_server_logger_tests
    server-logger-test.cpp
    ${QSAN_TEST_ROOT}/src/server/server-logger.cpp
    ${QSAN_TEST_ROOT}/src/server/server-logger.h
)
target_include_directories(qsanguosha_server_logger_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/server
)
target_link_libraries(qsanguosha_server_logger_tests PRIVATE Qt6::Core)
qsan_add_ctest(qsanguosha_server_logging qsanguosha_server_logger_tests
    LABELS "server;fast"
)
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
    add_executable(qsanguosha_network_integration_tests
        server-network-integration-test.cpp
    )
    target_include_directories(qsanguosha_network_integration_tests PRIVATE
        ${QSAN_TEST_ROOT}/src/core
    )
    target_link_libraries(qsanguosha_network_integration_tests PRIVATE
        qsanguosha_engine
        Qt6::Core
        Qt6::Network
    )
    add_test(NAME qsanguosha_network_integration
        COMMAND qsanguosha_network_integration_tests
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
    ${QSAN_TEST_ROOT}/src/server/server-core.h
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
    move-classifier-ordering/move-classifier-ordering-test.cpp
    extra-turn-scheduler-test.cpp
    room-roster-test.cpp
    player-lifecycle-service-test.cpp
    player-decision-service-test.cpp
    ${QSAN_TEST_ROOT}/src/server/server-core.h
)
qsan_link_engine_test(qsanguosha_server_tests)
qsan_add_ctest(qsanguosha_server_unit qsanguosha_server_tests
    LABELS server
    TIMEOUT 1800
)

add_executable(qsanguosha_replay_tests
    replay-game-state/replay-game-state-test.cpp
    ${QSAN_TEST_ROOT}/src/server/server-core.h
    ${QSAN_TEST_ROOT}/src/util/replay-game-state.cpp
    ${QSAN_TEST_ROOT}/src/util/replay-game-state.h
)
target_include_directories(qsanguosha_replay_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/core
    ${QSAN_TEST_ROOT}/src/util
)
target_link_libraries(qsanguosha_replay_tests PRIVATE
    qsanguosha_engine
    Qt6::Core
    Qt6::Core5Compat
)
qsan_add_ctest(qsanguosha_replay_game_state_protocol qsanguosha_replay_tests
    LABELS "protocol;fast"
)

add_executable(qsanguosha_ui_tests
    photo-layout-fit-test.cpp
)
target_include_directories(qsanguosha_ui_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/ui
)
qsan_add_ctest(qsanguosha_photo_layout_fit qsanguosha_ui_tests
    LABELS "ui;fast"
)
