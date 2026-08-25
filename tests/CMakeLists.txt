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
add_test(NAME "qsanguosha_card_lifetime:synthetic-50"
    COMMAND qsanguosha_runtime_tests --suite card-lifetime-synthetic-50 --seed 2026082201)
set_tests_properties("qsanguosha_card_lifetime:synthetic-50" PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}" LABELS runtime)
add_test(NAME "qsanguosha_card_lifetime:lua"
    COMMAND qsanguosha_runtime_tests --suite card-lifetime-lua)
set_tests_properties("qsanguosha_card_lifetime:lua" PROPERTIES
    WORKING_DIRECTORY "${QSAN_TEST_ROOT}" LABELS runtime)

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
