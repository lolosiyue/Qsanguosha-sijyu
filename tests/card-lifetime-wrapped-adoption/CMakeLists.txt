cmake_minimum_required(VERSION 3.24)
project(card_lifetime_wrapped_adoption LANGUAGES CXX)

if (NOT TARGET qsanguosha_engine)
    message(FATAL_ERROR "This isolated project requires the parent qsanguosha_engine target")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(card_lifetime_wrapped_adoption-test wrapped-adoption-test.cpp)
target_link_libraries(card_lifetime_wrapped_adoption-test PRIVATE qsanguosha_engine Qt6::Core)
target_include_directories(card_lifetime_wrapped_adoption-test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src
    ${CMAKE_CURRENT_LIST_DIR}/../../src/core)
