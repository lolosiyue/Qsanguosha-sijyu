set(QSAN_TEST_ROOT "${CMAKE_SOURCE_DIR}")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(QSAN_ENGINE_TEST_INCLUDES
    ${QSAN_TEST_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${QSAN_TEST_ROOT}/include
    ${QSAN_TEST_ROOT}/include/freetype
    ${QSAN_TEST_ROOT}/src/client
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
    cmake_parse_arguments(ARG "" "SUITE" "LABELS" ${ARGN})
    if(ARG_SUITE)
        add_test(NAME ${name} COMMAND ${executable} --suite ${ARG_SUITE})
    else()
        add_test(NAME ${name} COMMAND ${executable})
    endif()
    set_tests_properties(${name} PROPERTIES
        WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
        LABELS "${ARG_LABELS}"
    )
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
qsan_add_ctest(qsanguosha_card_overview_classifier
    qsanguosha_card_overview_classifier_tests
    LABELS "card-overview;fast"
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
qsan_add_ctest(qsanguosha_card_overview_model
    qsanguosha_card_overview_model_tests
    LABELS "card-overview;engine"
)

add_executable(qsanguosha_local_response_ui_case_tests
    skill_ui_runner/local-response-ui-case-test.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/local-response-ui-case.cpp
    ${QSAN_TEST_ROOT}/src/ui/testing/local-response-ui-case.h
)
qsan_link_engine_test(qsanguosha_local_response_ui_case_tests)
target_include_directories(qsanguosha_local_response_ui_case_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/ui/testing
)
qsan_add_ctest(qsanguosha_local_response_ui_case_parser
    qsanguosha_local_response_ui_case_tests
    LABELS "ui-runner;fast"
)
if(WIN32)
    set_property(TEST qsanguosha_local_response_ui_case_parser
        PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

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
qsan_add_ctest(qsanguosha_ui_startup_smoke_contract qsanguosha_ui_startup_smoke_tests
    LABELS "ui-runner;gui;fast"
)
if(WIN32)
    set_property(TEST qsanguosha_ui_startup_smoke_contract
        PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

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
qsan_add_ctest(qsanguosha_network_ui_smoke_contract qsanguosha_network_ui_smoke_tests
    LABELS "ui-runner;gui;network;fast"
)
if(WIN32)
    set_property(TEST qsanguosha_network_ui_smoke_contract
        PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

add_test(NAME qsanguosha_network_ui_smoke_cli_contract
    COMMAND ${Python3_EXECUTABLE}
        ${QSAN_TEST_ROOT}/tools/autotest/tests/test_gui_network_smoke_contract.py
)
set_tests_properties(qsanguosha_network_ui_smoke_cli_contract PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "ui-runner;gui;network;fast"
)

add_test(NAME qsanguosha_ui_startup_smoke_cli_contract
    COMMAND ${Python3_EXECUTABLE}
        ${QSAN_TEST_ROOT}/tools/autotest/tests/test_ui_startup_smoke_contract.py
)
set_tests_properties(qsanguosha_ui_startup_smoke_cli_contract PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "ui-runner;gui;fast"
)

add_test(NAME qsanguosha_skill_ui_runner_contract
    COMMAND ${Python3_EXECUTABLE}
        ${QSAN_TEST_ROOT}/tools/autotest/tests/test_skill_ui_runner_contract.py
)
set_tests_properties(qsanguosha_skill_ui_runner_contract PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "ui-runner;fast"
)

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
qsan_add_ctest(qsanguosha_server_cli_parser qsanguosha_cli_tests
    LABELS "cli;fast"
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
    LABELS "logging;server;fast"
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
add_test(NAME qsanguosha_server_cli_help COMMAND qsanguosha_server --help)
set_tests_properties(qsanguosha_server_cli_help PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "cli;fast"
    PASS_REGULAR_EXPRESSION "--bind-address"
)
add_test(NAME qsanguosha_server_cli_version COMMAND qsanguosha_server --version)
set_tests_properties(qsanguosha_server_cli_version PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "cli;fast"
    PASS_REGULAR_EXPRESSION "qsanguosha_server [0-9]+"
)
add_test(NAME qsanguosha_server_config_check
    COMMAND qsanguosha_server
        --config ${QSAN_TEST_ROOT}/docs/server.ini.example
        --check-config
)
set_tests_properties(qsanguosha_server_config_check PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "cli;server"
    PASS_REGULAR_EXPRESSION "Configuration OK"
)
add_test(NAME qsanguosha_server_config_precedence
    COMMAND qsanguosha_server
        --config ${QSAN_TEST_ROOT}/docs/server.ini.example
        --port 19527
        --print-config
        --json
)
set_tests_properties(qsanguosha_server_config_precedence PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
    LABELS "cli;server"
    PASS_REGULAR_EXPRESSION "\"DisableChat\": true.*\"GameMode\": \"10p\".*\"ServerPort\": 19527"
)
if(UNIX)
    add_test(NAME qsanguosha_server_console
        COMMAND bash ${QSAN_TEST_ROOT}/tools/ci/server-console-smoke.sh
            $<TARGET_FILE:qsanguosha_server>
            ${CMAKE_CURRENT_BINARY_DIR}/server-console-smoke.log
    )
    set_tests_properties(qsanguosha_server_console PROPERTIES
        WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
        LABELS "cli;server;smoke"
        TIMEOUT 45
    )
    add_test(NAME qsanguosha_server_logging_smoke
        COMMAND bash ${QSAN_TEST_ROOT}/tools/ci/server-logging-smoke.sh
            $<TARGET_FILE:qsanguosha_server>
            ${CMAKE_CURRENT_BINARY_DIR}/server-logging-smoke.log
    )
    set_tests_properties(qsanguosha_server_logging_smoke PROPERTIES
        WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
        LABELS "logging;server;smoke"
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
    foreach(network_level RANGE 1 3)
        add_test(NAME qsanguosha_network_integration_level${network_level}
            COMMAND qsanguosha_network_integration_tests
                --level ${network_level}
                --server $<TARGET_FILE:qsanguosha_server>
        )
        set_tests_properties(qsanguosha_network_integration_level${network_level} PROPERTIES
            WORKING_DIRECTORY "${QSAN_TEST_ROOT}"
            LABELS "network;server;integration"
            RUN_SERIAL TRUE
            TIMEOUT 360
        )
    endforeach()
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
qsan_add_ctest(qsanguosha_lua_runtime_isolation qsanguosha_runtime_tests
    SUITE lua-runtime
    LABELS runtime
)
qsan_add_ctest(qsanguosha_room_runtime_isolation qsanguosha_runtime_tests
    SUITE room-runtime
    LABELS runtime
)
qsan_add_ctest(qsanguosha_card_lifetime qsanguosha_runtime_tests
    SUITE card-lifetime
    LABELS runtime
)
add_test(NAME "qsanguosha_card_lifetime:source-check"
    COMMAND "${Python3_EXECUTABLE}" "${QSAN_TEST_ROOT}/tools/check-card-lifetime.py"
            --root "${QSAN_TEST_ROOT}"
            --ledger docs/card-lifetime-ownership.md
            --allowlist tools/card-lifetime-allowlist.json)
set_tests_properties("qsanguosha_card_lifetime:source-check" PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}" LABELS "runtime;source-check")
add_test(NAME "qsanguosha_card_lifetime:synthetic-30"
    COMMAND qsanguosha_runtime_tests --suite card-lifetime-synthetic-30 --seed 2026082201)
set_tests_properties("qsanguosha_card_lifetime:synthetic-30" PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}" LABELS runtime)
if(WIN32)
    set_property(TEST "qsanguosha_card_lifetime:synthetic-30" PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()
add_test(NAME "qsanguosha_card_lifetime:synthetic-50"
    COMMAND qsanguosha_runtime_tests --suite card-lifetime-synthetic-50 --seed 2026082201)
set_tests_properties("qsanguosha_card_lifetime:synthetic-50" PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}" LABELS runtime)
if(WIN32)
    set_property(TEST "qsanguosha_card_lifetime:synthetic-50" PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()
add_test(NAME "qsanguosha_card_lifetime:lua"
    COMMAND qsanguosha_runtime_tests --suite card-lifetime-lua)
set_tests_properties("qsanguosha_card_lifetime:lua" PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}" LABELS runtime)
if(WIN32)
    set_property(TEST "qsanguosha_card_lifetime:lua" PROPERTY ENVIRONMENT_MODIFICATION
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
qsan_add_ctest(qsanguosha_room_notifier qsanguosha_server_tests
    SUITE room-notifier
    LABELS server
)
qsan_add_ctest(qsanguosha_skill_runtime_coordinator qsanguosha_server_tests
    SUITE skill-runtime
    LABELS server
)
qsan_add_ctest(qsanguosha_request_coordinator qsanguosha_server_tests
    SUITE request
    LABELS server
)
qsan_add_ctest(qsanguosha_card_movement_service qsanguosha_server_tests
    SUITE card-movement
    LABELS server
)
qsan_add_ctest(qsanguosha_extra_turn_scheduler qsanguosha_server_tests
    SUITE extra-turn
    LABELS server
)
qsan_add_ctest(qsanguosha_room_roster qsanguosha_server_tests
    SUITE room-roster
    LABELS server
)
qsan_add_ctest(qsanguosha_player_lifecycle_service qsanguosha_server_tests
    SUITE player-lifecycle
    LABELS server
)
qsan_add_ctest(qsanguosha_player_decision_service qsanguosha_server_tests
    SUITE player-decision
    LABELS server
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
    LABELS protocol
)

add_executable(qsanguosha_ui_tests
    photo-layout-fit-test.cpp
)
target_include_directories(qsanguosha_ui_tests PRIVATE
    ${QSAN_TEST_ROOT}/src/ui
)
qsan_add_ctest(qsanguosha_photo_layout_fit qsanguosha_ui_tests
    LABELS ui
)
