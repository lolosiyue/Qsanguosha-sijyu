cmake_minimum_required(VERSION 3.28)
project(GeneralAuthoringContractTests LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

set(LUA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../src/lua")
set(LUA_PARSER_SOURCES
    ${LUA_ROOT}/lapi.c
    ${LUA_ROOT}/lauxlib.c
    ${LUA_ROOT}/lcode.c
    ${LUA_ROOT}/lctype.c
    ${LUA_ROOT}/ldebug.c
    ${LUA_ROOT}/ldo.c
    ${LUA_ROOT}/ldump.c
    ${LUA_ROOT}/lfunc.c
    ${LUA_ROOT}/lgc.c
    ${LUA_ROOT}/llex.c
    ${LUA_ROOT}/lmem.c
    ${LUA_ROOT}/lobject.c
    ${LUA_ROOT}/lopcodes.c
    ${LUA_ROOT}/lparser.c
    ${LUA_ROOT}/lstate.c
    ${LUA_ROOT}/lstring.c
    ${LUA_ROOT}/ltable.c
    ${LUA_ROOT}/ltm.c
    ${LUA_ROOT}/lundump.c
    ${LUA_ROOT}/lvm.c
    ${LUA_ROOT}/lzio.c
)

add_executable(general_authoring_tests
    test_general_authoring.cpp
    ../../src/dialog/general-authoring.cpp
    ../../src/dialog/general-authoring-provider.cpp
    ../../src/dialog/general-authoring-dialog.cpp
    ../../src/dialog/general-authoring-dialog.h
    ../../src/dialog/general-authoring.qrc
    ../../src/core/package-catalog.cpp
    ../../src/core/runtime-paths.cpp
    ${LUA_PARSER_SOURCES}
)
target_include_directories(general_authoring_tests PRIVATE
    ../../src/dialog
    ../../src/core
    ../../src/lua
)
target_link_libraries(general_authoring_tests PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)

enable_testing()
add_test(NAME general_authoring_contracts COMMAND general_authoring_tests)
# A top-level Lua loop is included as parse-only input. A regression that starts
# executing candidate source instead of compiling it must fail this bounded test.
set_tests_properties(general_authoring_contracts PROPERTIES TIMEOUT 8 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
