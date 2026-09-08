cmake_minimum_required(VERSION 3.28)
project(QSanRulesIdentityContract LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core)
get_filename_component(QSAN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_executable(qsan_rules_identity_contract
    contract-test.cpp
    "${QSAN_ROOT}/src/core/protocol/session/session-payloads.cpp")
target_include_directories(qsan_rules_identity_contract PRIVATE "${QSAN_ROOT}/src/core")
target_link_libraries(qsan_rules_identity_contract PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(qsan_rules_identity_contract PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME rules_identity_contract COMMAND qsan_rules_identity_contract
    "${CMAKE_CURRENT_LIST_DIR}/vectors.json")
