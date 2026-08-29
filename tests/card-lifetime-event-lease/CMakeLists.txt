cmake_minimum_required(VERSION 3.24)
project(card_lifetime_event_lease LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QSAN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(QSAN_BUILD "${QSAN_ROOT}/builds/cmake-vs2026")
set(CMAKE_PREFIX_PATH "H:/Qt6111/6.11.1/msvc2022_64")

find_package(Qt6 REQUIRED COMPONENTS Core Qml Widgets)

add_executable(card_lifetime_event_lease_test
    card-lifetime-event-lease-test.cpp
    "${QSAN_ROOT}/src/server/server-core.h"
)
target_include_directories(card_lifetime_event_lease_test PRIVATE
    "${QSAN_ROOT}"
    "${QSAN_ROOT}/src/core"
    "${QSAN_ROOT}/src/server"
    "${QSAN_ROOT}/src/lua"
    "${QSAN_ROOT}/include"
    "${QSAN_ROOT}/include/freetype"
)
target_link_libraries(card_lifetime_event_lease_test PRIVATE
    "${QSAN_BUILD}/Debug/qsanguosha_engine.lib"
    Qt6::Core
    Qt6::Qml
    Qt6::Widgets
)
set_target_properties(card_lifetime_event_lease_test PROPERTIES
    WIN32_EXECUTABLE OFF
)
