# Focused Android content importer regression target. Included by Linux CI.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(ZLIB REQUIRED)
set(QSAN_ANDROID_CONTENT_FIXTURE_ROOT "${CMAKE_SOURCE_DIR}/tests/fixtures/android-content")

set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/base-descriptor.fixture" PROPERTIES QT_RESOURCE_ALIAS "runtime-content-base.json")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-inventory.fixture" PROPERTIES QT_RESOURCE_ALIAS "media-inventory.json")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/base-script.fixture" PROPERTIES QT_RESOURCE_ALIAS "extensions/base.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/bundled-addon.fixture" PROPERTIES QT_RESOURCE_ALIAS "extensions/addon.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-config.fixture" PROPERTIES QT_RESOURCE_ALIAS "lua/config.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-sanguosha.fixture" PROPERTIES QT_RESOURCE_ALIAS "lua/sanguosha.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-ai.fixture" PROPERTIES QT_RESOURCE_ALIAS "lua/ai/smart-ai.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-image.fixture" PROPERTIES QT_RESOURCE_ALIAS "image/base.png")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-audio.fixture" PROPERTIES QT_RESOURCE_ALIAS "audio/base.ogg")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-font.fixture" PROPERTIES QT_RESOURCE_ALIAS "font/base.ttf")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/asset-revision.fixture" PROPERTIES QT_RESOURCE_ALIAS "android-content-revision.txt")

add_executable(qsanguosha_android_content_store_tests
    "${CMAKE_SOURCE_DIR}/tests/android-content-store-test.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/android-content-store.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/android-content-store.h"
    "${CMAKE_SOURCE_DIR}/src/core/android-zip-reader.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/android-zip-reader.h"
    "${CMAKE_SOURCE_DIR}/src/core/android_assets.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/android_assets.h"
    "${CMAKE_SOURCE_DIR}/src/core/rules-content-manifest.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/rules-content-manifest.h")
qt_add_resources(qsanguosha_android_content_store_tests android_content_fixture
    PREFIX "/assets" BASE "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}"
    FILES
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/base-descriptor.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-inventory.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/base-script.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/bundled-addon.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-config.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-sanguosha.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-ai.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-image.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-audio.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-font.fixture")
qt_add_resources(qsanguosha_android_content_store_tests android_content_revision_fixture
    PREFIX "/"
    FILES "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/asset-revision.fixture")
target_include_directories(qsanguosha_android_content_store_tests PRIVATE "${CMAKE_SOURCE_DIR}/src/core")
target_link_libraries(qsanguosha_android_content_store_tests PRIVATE Qt6::Core ZLIB::ZLIB)
set_target_properties(qsanguosha_android_content_store_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(qsanguosha_android_content_store_tests PRIVATE /utf-8)
endif()
qsan_add_ctest(qsanguosha_android_content_store qsanguosha_android_content_store_tests
    LABELS "android;content;fast" TIMEOUT 60)
