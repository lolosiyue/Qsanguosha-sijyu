# Both XP products consume the same engine and source fingerprint. This target
# is independent of QSAN_BUILD_SERVER's modern product defaults.
file(GLOB_RECURSE qsan_xp_pair_sources CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/legacy/xp/src/*.cpp" "${CMAKE_SOURCE_DIR}/legacy/xp/src/*.h"
    "${CMAKE_SOURCE_DIR}/legacy/xp/compat/*.h"
    "${CMAKE_SOURCE_DIR}/legacy/xp/cmake/*.cmake"
    "${CMAKE_SOURCE_DIR}/swig/*.i")
list(APPEND qsan_xp_pair_sources "${CMAKE_SOURCE_DIR}/CMakeLists.txt")
list(SORT qsan_xp_pair_sources)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${qsan_xp_pair_sources})
set(qsan_xp_pair_hashes "")
foreach(source IN LISTS qsan_xp_pair_sources)
    file(SHA256 "${source}" digest)
    string(APPEND qsan_xp_pair_hashes "${digest}")
endforeach()

string(SHA256 QSAN_XP_BUILD_ID
    "${qsan_xp_pair_hashes};${CMAKE_CXX_COMPILER_VERSION};${CMAKE_GENERATOR_TOOLSET};${Qt5Core_VERSION}")

function(qsan_xp_target target)
    target_include_directories(${target} BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/legacy/xp/compat/qt5"
        "${CMAKE_SOURCE_DIR}/legacy/xp/src")
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/core"
        "${CMAKE_SOURCE_DIR}/src/server" "${CMAKE_SOURCE_DIR}/src/util"
        "${CMAKE_SOURCE_DIR}/src/lua" "${CMAKE_SOURCE_DIR}/src/package"
        "${CMAKE_SOURCE_DIR}/src/scenario")
    target_compile_definitions(${target} PRIVATE QSAN_XP_LEGACY
        QSAN_XP_BUILD_ID="${QSAN_XP_BUILD_ID}-$<CONFIG>"
        QSAN_ENABLE_QML=0 QSAN_ENABLE_SPINE=0 QSAN_ENABLE_VIDEO=0
        QSAN_ENABLE_WEBSOCKETS=0 QSAN_USE_RASTER_VIEWPORT=1
        WIN32 WINVER=0x0501 _WIN32_WINNT=0x0501 _WIN32_IE=0x0600
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(${target} PRIVATE /utf-8 /bigobj /MP8 /Zc:threadSafeInit-
        "/FI${CMAKE_SOURCE_DIR}/legacy/xp/compat/qt5/qsan-qt5-compat.h")
    target_link_options(${target} PRIVATE /SUBSYSTEM:CONSOLE,5.01
        "$<$<CONFIG:Debug>:/NODEFAULTLIB:MSVCRT>")
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/xp-debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/xp-release")
endfunction()

add_library(qsan_xp_control STATIC
    legacy/xp/src/xp-control-protocol.cpp legacy/xp/src/xp-control-protocol.h)
qsan_xp_target(qsan_xp_control)
target_link_libraries(qsan_xp_control PUBLIC Qt5::Core Qt5::Network advapi32)

add_executable(QSanguoshaXPServer
    legacy/xp/src/xp-server-main.cpp ${QSAN_DEDICATED_ENTRY_SOURCES})
qsan_xp_target(QSanguoshaXPServer)
target_compile_definitions(QSanguoshaXPServer PRIVATE QSAN_ENGINE_BUILD QSAN_SERVER_CORE_ONLY)
target_link_libraries(QSanguoshaXPServer PRIVATE qsanguosha_engine qsan_xp_control
    Ws2_32 IPHLPAPI dbghelp user32 gdi32)
target_link_options(QSanguoshaXPServer PRIVATE
    "/WHOLEARCHIVE:$<TARGET_FILE:qsanguosha_engine>" "$<$<CONFIG:Release>:/DEBUG>")

get_target_property(qsan_xp_server_links QSanguoshaXPServer LINK_LIBRARIES)
foreach(link IN LISTS qsan_xp_server_links)
    if(link MATCHES "Widgets|Quick|Qml|Multimedia|OpenGL|fmod|spine")
        message(FATAL_ERROR "XP server has a forbidden dependency: ${link}")
    endif()
endforeach()
