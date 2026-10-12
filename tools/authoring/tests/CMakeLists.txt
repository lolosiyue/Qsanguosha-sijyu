cmake_minimum_required(VERSION 3.21)
project(authoring_cache_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
# Standalone document/serialization fixtures use only Qt Core APIs available
# in Qt 6.8. This does not lower the application GUI's Qt 6.11 requirement.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
# Build only the bundled Lua implementation; no engine/server dependencies.
file(GLOB QSAN_LUA_SOURCES CONFIGURE_DEPENDS "${REPO}/src/lua/*.c")
add_library(authoring_test_lua STATIC ${QSAN_LUA_SOURCES})
target_include_directories(authoring_test_lua PUBLIC "${REPO}/src/lua")
if(UNIX)
    target_link_libraries(authoring_test_lua PUBLIC m ${CMAKE_DL_LIBS})
endif()
add_executable(authoring_cache_test cache-test.cpp
    "${REPO}/src/dialog/general-authoring.cpp"
    "${REPO}/src/dialog/general-authoring.qrc")
target_include_directories(authoring_cache_test PRIVATE "${REPO}/src/dialog")
target_link_libraries(authoring_cache_test PRIVATE Qt6::Core authoring_test_lua)
enable_testing()
add_test(NAME authoring_cache COMMAND authoring_cache_test)
