# These tests exercise the managed value-world boundary without booting Room or
# loading native packages. Successful tests do not imply live Room rollback support.
get_filename_component(qsan_contract_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_executable(qsanguosha_game_state_contract_tests
    ${qsan_contract_root}/tests/game-state-contract-test.cpp
    ${qsan_contract_root}/src/core/game-state-contract.cpp
    ${qsan_contract_root}/src/core/game-timeline.cpp
    ${qsan_contract_root}/src/core/game-rng.cpp
)
target_include_directories(qsanguosha_game_state_contract_tests PRIVATE ${qsan_contract_root}/src/core)
target_link_libraries(qsanguosha_game_state_contract_tests PRIVATE Qt6::Core)
add_test(NAME qsanguosha_game_state_contract COMMAND qsanguosha_game_state_contract_tests)
set_tests_properties(qsanguosha_game_state_contract PROPERTIES LABELS "state-contract;fast")

add_executable(qsanguosha_game_state_runtime_values_tests
    ${qsan_contract_root}/tests/game-state-runtime-values-test.cpp
    ${qsan_contract_root}/src/core/game-rng.cpp
    ${qsan_contract_root}/src/core/resolution-history.cpp)
target_include_directories(qsanguosha_game_state_runtime_values_tests PRIVATE ${qsan_contract_root}/src/core)
target_link_libraries(qsanguosha_game_state_runtime_values_tests PRIVATE Qt6::Core)
add_test(NAME qsanguosha_game_state_runtime_values COMMAND qsanguosha_game_state_runtime_values_tests)
set_tests_properties(qsanguosha_game_state_runtime_values PROPERTIES LABELS "state-contract;fast")

# Compile the engine's Lua dialect; do not depend on a system Lua executable.
add_executable(qsanguosha_managed_state_lua_tests ${qsan_contract_root}/tests/managed-state-lua-main.cpp)
foreach(qsan_lua_source IN LISTS QSAN_LUA_SOURCES)
    target_sources(qsanguosha_managed_state_lua_tests PRIVATE ${qsan_contract_root}/${qsan_lua_source})
endforeach()
set_target_properties(qsanguosha_managed_state_lua_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(qsanguosha_managed_state_lua_tests PRIVATE ${qsan_contract_root}/src/lua)
if(UNIX)
    target_link_libraries(qsanguosha_managed_state_lua_tests PRIVATE m ${CMAKE_DL_LIBS})
endif()
add_test(NAME qsanguosha_managed_state_lua COMMAND qsanguosha_managed_state_lua_tests
    tests/lua-game-state-contract-test.lua)
set_tests_properties(qsanguosha_managed_state_lua PROPERTIES
    WORKING_DIRECTORY ${qsan_contract_root} LABELS "state-contract;lua;fast")

# The standalone QtCore entry deliberately omits this production Room test.
if(TARGET qsanguosha_engine)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(QSAN_EXTENSIONS_SOURCE_DIR "${qsan_contract_root}/../extensions" CACHE PATH
        "Companion checkout for focused engine tests")
    set(qsan_managed_runtime "${CMAKE_BINARY_DIR}/live-runtime")
    add_test(NAME qsanguosha_managed_state_prepare_runtime COMMAND ${Python3_EXECUTABLE}
        ${qsan_contract_root}/tools/autotest/stage_external_agent_runtime.py
        --engine ${qsan_contract_root} --extensions ${QSAN_EXTENSIONS_SOURCE_DIR}
        --output ${qsan_managed_runtime})
    set_tests_properties(qsanguosha_managed_state_prepare_runtime PROPERTIES
        FIXTURES_SETUP managed_state_runtime LABELS "state-contract;live")
    add_executable(qsanguosha_room_managed_state_tests
        ${qsan_contract_root}/tests/room-managed-state-test.cpp)
    target_compile_definitions(qsanguosha_room_managed_state_tests PRIVATE
        QSAN_ENGINE_BUILD QSAN_SERVER_CORE_ONLY)
    get_target_property(qsan_managed_engine_includes qsanguosha_engine INCLUDE_DIRECTORIES)
    target_include_directories(qsanguosha_room_managed_state_tests PRIVATE ${qsan_managed_engine_includes})
    target_link_libraries(qsanguosha_room_managed_state_tests PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>")
    qsan_link_websocket_gateway(qsanguosha_room_managed_state_tests)
    add_test(NAME qsanguosha_room_managed_state COMMAND qsanguosha_room_managed_state_tests)
    set_tests_properties(qsanguosha_room_managed_state PROPERTIES
        FIXTURES_REQUIRED managed_state_runtime RUN_SERIAL TRUE TIMEOUT 60
        WORKING_DIRECTORY ${qsan_contract_root} LABELS "state-contract;live"
        ENVIRONMENT "QSAN_ASSET_ROOT=${qsan_managed_runtime};QSAN_USER_DATA_ROOT=${qsan_managed_runtime}")
    add_executable(qsanguosha_room_managed_turn_tests ${qsan_contract_root}/tests/room-managed-turn-test.cpp)
    target_compile_definitions(qsanguosha_room_managed_turn_tests PRIVATE QSAN_ENGINE_BUILD QSAN_SERVER_CORE_ONLY)
    target_include_directories(qsanguosha_room_managed_turn_tests PRIVATE ${qsan_managed_engine_includes})
    target_link_libraries(qsanguosha_room_managed_turn_tests PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>")
    qsan_link_websocket_gateway(qsanguosha_room_managed_turn_tests)
    add_test(NAME qsanguosha_room_managed_turn COMMAND qsanguosha_room_managed_turn_tests)
    set_tests_properties(qsanguosha_room_managed_turn PROPERTIES
        FIXTURES_REQUIRED managed_state_runtime RUN_SERIAL TRUE TIMEOUT 60
        WORKING_DIRECTORY ${qsan_contract_root} LABELS "state-contract;live"
        ENVIRONMENT "QSAN_ASSET_ROOT=${qsan_managed_runtime};QSAN_USER_DATA_ROOT=${qsan_managed_runtime}")
    add_executable(qsanguosha_managed_client_sync_tests ${qsan_contract_root}/tests/client-state-sync-test.cpp)
    target_include_directories(qsanguosha_managed_client_sync_tests PRIVATE ${qsan_contract_root}/src)
    target_link_libraries(qsanguosha_managed_client_sync_tests PRIVATE qsanguosha_client_core qsanguosha_engine)
    add_test(NAME qsanguosha_managed_client_sync COMMAND qsanguosha_managed_client_sync_tests)
    set_tests_properties(qsanguosha_managed_client_sync PROPERTIES LABELS "state-contract;fast")
    add_executable(qsanguosha_managed_rewind_network_tests ${qsan_contract_root}/tests/managed-rewind-network-test.cpp)
    target_compile_definitions(qsanguosha_managed_rewind_network_tests PRIVATE QSAN_ENGINE_BUILD QSAN_SERVER_CORE_ONLY)
    target_include_directories(qsanguosha_managed_rewind_network_tests PRIVATE ${qsan_managed_engine_includes} ${qsan_contract_root}/src)
    target_link_libraries(qsanguosha_managed_rewind_network_tests PRIVATE qsanguosha_client_core "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>")
    qsan_link_websocket_gateway(qsanguosha_managed_rewind_network_tests)
    add_test(NAME qsanguosha_managed_rewind_network COMMAND qsanguosha_managed_rewind_network_tests)
    set_tests_properties(qsanguosha_managed_rewind_network PROPERTIES FIXTURES_REQUIRED managed_state_runtime RUN_SERIAL TRUE TIMEOUT 60
        WORKING_DIRECTORY ${qsan_contract_root} LABELS "state-contract;live;network"
        ENVIRONMENT "QSAN_ASSET_ROOT=${qsan_managed_runtime};QSAN_USER_DATA_ROOT=${qsan_managed_runtime}")
    if(TARGET qsanguosha_rewind_lab)
        add_test(NAME qsanguosha_rewind_lab COMMAND ${Python3_EXECUTABLE}
            ${qsan_contract_root}/tests/rewind-lab-test.py
            $<TARGET_FILE:qsanguosha_rewind_lab> ${qsan_managed_runtime})
        set_tests_properties(qsanguosha_rewind_lab PROPERTIES
            FIXTURES_REQUIRED managed_state_runtime RUN_SERIAL TRUE TIMEOUT 60
            LABELS "state-contract;live")
    endif()
endif()
