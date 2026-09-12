# Android CP1 configuration shared by the Android GUI target and its package
# finalization. This file is included only after the Qt Android kit is found.

if(NOT ANDROID)
    return()
endif()

# qt_add_executable() creates a shared MODULE library on Android. Every static
# project library that is linked into that module therefore needs PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(QSAN_ANDROID_ABI "arm64-v8a" CACHE STRING
    "ABI used by the supported Android package")
set(QSAN_ANDROID_MIN_API 28 CACHE STRING
    "Minimum Android API level supported by the application")
set(QSAN_ANDROID_API 36 CACHE STRING
    "Android compile and target API level")
set(QSAN_ANDROID_NDK_VERSION "27.2.12479018" CACHE STRING
    "Required Android NDK version")
set(QSAN_ANDROID_FREETYPE_ROOT "$ENV{QSAN_ANDROID_FREETYPE_ROOT}" CACHE PATH
    "Prefix containing the Android arm64-v8a static FreeType package")

if(NOT Qt6_VERSION VERSION_EQUAL "6.11.1")
    message(FATAL_ERROR
        "Android requires the Qt 6.11.1 kit (found Qt ${Qt6_VERSION})")
endif()

if(NOT ANDROID_ABI STREQUAL "${QSAN_ANDROID_ABI}")
    message(FATAL_ERROR
        "Android requires ANDROID_ABI=${QSAN_ANDROID_ABI} (got '${ANDROID_ABI}')")
endif()

# CMake reports only major.minor here (27.2); source.properties has the package revision.
file(STRINGS "${CMAKE_ANDROID_NDK}/source.properties" qsan_android_ndk_revision
    REGEX "^Pkg\\.Revision[ \t]*=")
string(REGEX REPLACE "^[^=]*=[ \t]*" "" qsan_android_ndk_revision "${qsan_android_ndk_revision}")
if(NOT qsan_android_ndk_revision STREQUAL "${QSAN_ANDROID_NDK_VERSION}")
    message(FATAL_ERROR
        "Android requires NDK ${QSAN_ANDROID_NDK_VERSION} "
        "(found ${qsan_android_ndk_revision})")
endif()

if(NOT IS_DIRECTORY "${QSAN_ANDROID_FREETYPE_ROOT}")
    message(FATAL_ERROR
        "QSAN_ANDROID_FREETYPE_ROOT must name the installed Android FreeType "
        "prefix: '${QSAN_ANDROID_FREETYPE_ROOT}'")
endif()

# Seed FindFreetype's cache entries from the explicitly supplied target prefix.
# This keeps the normal FindFreetype target and usage requirements while making
# it impossible for a host desktop FreeType to satisfy the Android configure.
find_path(QSAN_ANDROID_FREETYPE_FT2BUILD_DIR
    NAMES ft2build.h
    PATHS
        "${QSAN_ANDROID_FREETYPE_ROOT}/include"
        "${QSAN_ANDROID_FREETYPE_ROOT}/include/freetype2"
        "${QSAN_ANDROID_FREETYPE_ROOT}/include/freetype"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)
find_path(QSAN_ANDROID_FREETYPE_HEADER_DIR
    NAMES freetype/config/ftheader.h freetype2/freetype/config/ftheader.h
    PATHS
        "${QSAN_ANDROID_FREETYPE_ROOT}/include"
        "${QSAN_ANDROID_FREETYPE_ROOT}/include/freetype2"
        "${QSAN_ANDROID_FREETYPE_ROOT}/include/freetype"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)
find_library(QSAN_ANDROID_FREETYPE_LIBRARY
    NAMES freetype libfreetype
    PATHS
        "${QSAN_ANDROID_FREETYPE_ROOT}/lib"
        "${QSAN_ANDROID_FREETYPE_ROOT}/lib/${ANDROID_ABI}"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)
if(NOT QSAN_ANDROID_FREETYPE_FT2BUILD_DIR
        OR NOT QSAN_ANDROID_FREETYPE_HEADER_DIR
        OR NOT QSAN_ANDROID_FREETYPE_LIBRARY)
    message(FATAL_ERROR
        "Android FreeType prefix must provide ft2build.h, FreeType headers, "
        "and a static libfreetype.a under '${QSAN_ANDROID_FREETYPE_ROOT}'")
endif()

set(FREETYPE_INCLUDE_DIR_ft2build "${QSAN_ANDROID_FREETYPE_FT2BUILD_DIR}"
    CACHE PATH "Android FreeType ft2build.h directory" FORCE)
set(FREETYPE_INCLUDE_DIR_freetype2 "${QSAN_ANDROID_FREETYPE_HEADER_DIR}"
    CACHE PATH "Android FreeType header directory" FORCE)
set(FREETYPE_INCLUDE_DIRS
    "${QSAN_ANDROID_FREETYPE_FT2BUILD_DIR};${QSAN_ANDROID_FREETYPE_HEADER_DIR}"
    CACHE STRING "Android FreeType include directories" FORCE)
set(FREETYPE_LIBRARY "${QSAN_ANDROID_FREETYPE_LIBRARY}"
    CACHE FILEPATH "Android FreeType static library" FORCE)
set(FREETYPE_LIBRARY_RELEASE "${QSAN_ANDROID_FREETYPE_LIBRARY}"
    CACHE FILEPATH "Android FreeType release static library" FORCE)
set(FREETYPE_LIBRARY_DEBUG "${QSAN_ANDROID_FREETYPE_LIBRARY}"
    CACHE FILEPATH "Android FreeType debug static library" FORCE)
set(Freetype_ROOT "${QSAN_ANDROID_FREETYPE_ROOT}")
find_package(Freetype 2.14.3 EXACT REQUIRED MODULE)

if(NOT TARGET Freetype::Freetype)
    message(FATAL_ERROR "FindFreetype did not provide Freetype::Freetype")
endif()
get_target_property(qsan_android_freetype_type Freetype::Freetype TYPE)
if(qsan_android_freetype_type STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "Android FreeType must be linked as a static library")
endif()
get_target_property(qsan_android_freetype_location
    Freetype::Freetype IMPORTED_LOCATION)
if(NOT qsan_android_freetype_location)
    set(qsan_android_freetype_location "${QSAN_ANDROID_FREETYPE_LIBRARY}")
endif()
if(NOT qsan_android_freetype_location MATCHES "\\.a$")
    message(FATAL_ERROR
        "Android FreeType must resolve to a .a archive "
        "(got '${qsan_android_freetype_location}')")
endif()

# Qt's qt_add_executable() creates the Android MODULE target and, during
# finalization, the target_make_apk/apk build target. Suppress unrelated AAB
# and AAR conveniences because this release is APK-only.
set(QT_NO_GLOBAL_AAB_TARGET TRUE CACHE BOOL
    "Do not create an Android App Bundle target for the APK-only build" FORCE)
set(QT_NO_GLOBAL_AAR_TARGET TRUE CACHE BOOL
    "Do not create an Android Archive target for the APK-only build" FORCE)

# Only Qt supplies shared dependencies; FreeType, Lua and Spine are static.
# Qt's optional non-Qt imported-library scan cannot defer into the closed
# target-factory directory. Future third-party shared libs must be listed in
# QT_ANDROID_EXTRA_LIBS explicitly; normal Qt dependency deployment stays on.
set(QT_NO_COLLECT_IMPORTED_TARGET_APK_DEPS TRUE)

function(qsan_configure_android_target target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Android target does not exist: ${target}")
    endif()

    get_filename_component(qsan_android_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    set_target_properties("${target}" PROPERTIES
        QT_ANDROID_ABIS "${QSAN_ANDROID_ABI}"
        QT_ANDROID_MIN_SDK_VERSION "${QSAN_ANDROID_MIN_API}"
        QT_ANDROID_TARGET_SDK_VERSION "${QSAN_ANDROID_API}"
        QT_ANDROID_COMPILE_SDK_VERSION "${QSAN_ANDROID_API}"
        QT_ANDROID_SDK_BUILD_TOOLS_REVISION "36.0.0"
        QT_ANDROID_PACKAGE_NAME "org.qsanguosha.game"
        QT_ANDROID_APP_NAME "QSanguosha"
        QT_ANDROID_PACKAGE_SOURCE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/android"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_VERSION_NAME "1.0"
        QT_ANDROID_NO_DEPLOY_QT_LIBS OFF
        # qmlimportscanner otherwise falls back to the target SOURCE_DIR. The
        # Android target lives under cmake/android-app, so list the two source
        # roots that contain application QML explicitly.
        QT_QML_IMPORT_PATH "${qsan_android_qt_prefix}/qml"
        QT_QML_ROOT_PATH
            "${CMAKE_SOURCE_DIR}/qml;${CMAKE_SOURCE_DIR}/ui-script"
    )

    target_compile_definitions("${target}" PRIVATE QSAN_ANDROID)

    # NDK r27 and lower need both flags for 16 KiB ELF page alignment.
    target_link_options("${target}" PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,-z,common-page-size=16384"
    )
endfunction()
