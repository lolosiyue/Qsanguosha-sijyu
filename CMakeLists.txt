cmake_minimum_required(VERSION 3.28)

project(QSanguosha LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This CMake build currently supports Windows x64 with MSVC only")
endif()
if(MSVC_VERSION LESS 1920 OR MSVC_VERSION GREATER_EQUAL 1930)
    message(FATAL_ERROR "This CMake build currently supports the Visual Studio 2019 toolchain only")
endif()

enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Core5Compat
    Gui
    LinguistTools
    Network
    OpenGLWidgets
    Qml
    Quick
    QuickWidgets
    Widgets
)

include(cmake/QSanguoshaSources.cmake)

qt_add_executable(QSanguosha WIN32
    ${QSAN_SOURCES}
    ${QSAN_LUA_SOURCES}
    ${QSAN_MOC_HEADERS}
    ${QSAN_FORMS}
    resource/icon.rc
)

target_sources(QSanguosha PRIVATE
    "$<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/src/core/audio.cpp>"
    "$<$<CONFIG:Release>:${QSAN_BREAKPAD_SOURCES}>"
)

target_include_directories(QSanguosha PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/freetype
    ${CMAKE_CURRENT_SOURCE_DIR}/include/spine-cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dialog
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/src/package
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    "$<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/include/fmod>"
    "$<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/src/breakpad>"
    "$<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/src/breakpad/client/windows>"
)

target_compile_definitions(QSanguosha PRIVATE
    USING_PCH
    WIN64
    _CRT_SECURE_NO_WARNINGS
    QT_DEPRECATED_WARNINGS
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    "$<$<CONFIG:Release>:AUDIO_SUPPORT>"
    "$<$<CONFIG:Release>:USE_BREAKPAD>"
)

target_compile_options(QSanguosha PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
    "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/bigobj>"
)

target_precompile_headers(QSanguosha PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h>"
)

set(qsan_library_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib/win/x64")
foreach(required_library freetype.lib freetype_D.lib fmodex.lib)
    if(NOT EXISTS "${qsan_library_dir}/${required_library}")
        message(FATAL_ERROR "Missing bundled library: ${qsan_library_dir}/${required_library}")
    endif()
endforeach()

target_link_libraries(QSanguosha PRIVATE
    Qt6::Core
    Qt6::Core5Compat
    Qt6::Gui
    Qt6::Network
    Qt6::OpenGLWidgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Widgets
    dwmapi
    legacy_stdio_definitions
    winhttp
    "$<$<CONFIG:Debug>:${qsan_library_dir}/freetype_D.lib>"
    "$<$<NOT:$<CONFIG:Debug>>:${qsan_library_dir}/freetype.lib>"
    "$<$<CONFIG:Release>:${qsan_library_dir}/fmodex.lib>"
)

target_link_options(QSanguosha PRIVATE "$<$<CONFIG:Release>:/DEBUG>")

set_target_properties(QSanguosha PROPERTIES
    QT_QML_MODULE_NO_IMPORT_SCAN TRUE
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/release"
    PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/debug"
    PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/release"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(swig_wrapper "${CMAKE_CURRENT_SOURCE_DIR}/swig/sanguosha_wrap.cxx")
add_custom_target(check-swig-wrapper
    COMMAND ${CMAKE_COMMAND}
        -DQSAN_SWIG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/swig
        -DQSAN_SWIG_WRAPPER=${swig_wrapper}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckSwigWrapper.cmake
    VERBATIM
)
add_dependencies(QSanguosha check-swig-wrapper)

set(swig_executable "${CMAKE_CURRENT_SOURCE_DIR}/tools/swig/swig.exe")
add_custom_target(swig-regenerate
    COMMAND "${swig_executable}" -c++ -lua -o "${swig_wrapper}"
        "${CMAKE_CURRENT_SOURCE_DIR}/swig/sanguosha.i"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Regenerating swig/sanguosha_wrap.cxx"
    VERBATIM
)

set(translation_source "${CMAKE_CURRENT_SOURCE_DIR}/builds/sanguosha.ts")
set_source_files_properties("${translation_source}" PROPERTIES
    OUTPUT_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}"
)
qt_add_lrelease(QSanguosha
    TS_FILES "${translation_source}"
    QM_FILES_OUTPUT_VARIABLE qsan_qm_files
)

set(QSAN_FMOD_RUNTIME "" CACHE FILEPATH "Path to fmodex64.dll for Release deployment")
find_program(QSAN_WINDEPLOYQT
    NAMES windeployqt
    HINTS "${QT6_INSTALL_PREFIX}/bin"
    NO_DEFAULT_PATH
    REQUIRED
)
add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND}
        -DQSAN_CONFIG=$<CONFIG>
        -DQSAN_EXECUTABLE=$<TARGET_FILE:QSanguosha>
        -DQSAN_WINDEPLOYQT=${QSAN_WINDEPLOYQT}
        -DQSAN_QML_DIR=${CMAKE_CURRENT_SOURCE_DIR}/ui-script
        -DQSAN_FMOD_RUNTIME=${QSAN_FMOD_RUNTIME}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Deploy.cmake
    DEPENDS QSanguosha
    VERBATIM
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${QSAN_SOURCES}
    ${QSAN_LUA_SOURCES}
    ${QSAN_MOC_HEADERS}
    ${QSAN_FORMS}
)
