# The store is QtCore-only; exercise its snapshot contract on desktop too.
if(ANDROID OR EMSCRIPTEN OR QSAN_BUILD_XP_LEGACY)
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
if(QSAN_PACKAGE_ZLIB)
    set(qsan_content_zlib ${QSAN_PACKAGE_ZLIB})
else()
    find_package(ZLIB REQUIRED)
    set(qsan_content_zlib ZLIB::ZLIB)
endif()
set(QSAN_ANDROID_CONTENT_FIXTURE_ROOT "${CMAKE_SOURCE_DIR}/tests/fixtures/android-content")

set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/base-descriptor.fixture" PROPERTIES QT_RESOURCE_ALIAS "runtime-content-base.json")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-inventory.fixture" PROPERTIES QT_RESOURCE_ALIAS "media-inventory.json")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/base-script.fixture" PROPERTIES QT_RESOURCE_ALIAS "extensions/base.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/bundled-addon.fixture" PROPERTIES QT_RESOURCE_ALIAS "extensions/addon.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/addon-translation.fixture" PROPERTIES QT_RESOURCE_ALIAS "lang/zh_CN/Audio/AddedPackageLines.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-config.fixture" PROPERTIES QT_RESOURCE_ALIAS "lua/config.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-sanguosha.fixture" PROPERTIES QT_RESOURCE_ALIAS "lua/sanguosha.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-sgs-ex.fixture" PROPERTIES QT_RESOURCE_ALIAS "lua/sgs_ex.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-ai.fixture" PROPERTIES QT_RESOURCE_ALIAS "lua/ai/smart-ai.lua")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-image.fixture" PROPERTIES QT_RESOURCE_ALIAS "image/base.png")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-audio.fixture" PROPERTIES QT_RESOURCE_ALIAS "audio/base.ogg")
set_source_files_properties("${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-font.fixture" PROPERTIES QT_RESOURCE_ALIAS "font/base.ttf")
# The runtime receipt reader bounds this resource to 65 bytes; normalize CRLF
# checkouts to the fixture's exact 64-byte digest before embedding it in Qt.
file(READ "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/asset-revision.fixture" qsan_android_revision_fixture_text)
string(STRIP "${qsan_android_revision_fixture_text}" qsan_android_revision_fixture_text)
string(LENGTH "${qsan_android_revision_fixture_text}" qsan_android_revision_fixture_length)
if(NOT qsan_android_revision_fixture_length EQUAL 64
    OR NOT qsan_android_revision_fixture_text MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "Android content test revision fixture must contain exactly 64 hexadecimal bytes")
endif()
set(qsan_android_revision_fixture "${CMAKE_CURRENT_BINARY_DIR}/generated/android-content-revision-fixture.txt")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${qsan_android_revision_fixture}" "${qsan_android_revision_fixture_text}")
set_source_files_properties("${qsan_android_revision_fixture}" PROPERTIES
    GENERATED TRUE QT_RESOURCE_ALIAS "android-content-revision.txt")

add_executable(qsanguosha_android_content_store_tests
    "${CMAKE_SOURCE_DIR}/tests/android-content-store-test.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/android-content-store.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/android-content-store.h"
    "${CMAKE_SOURCE_DIR}/src/core/android-zip-reader.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/android-zip-reader.h"
    "${CMAKE_SOURCE_DIR}/src/core/android_assets.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/android_assets.h"
    "${CMAKE_SOURCE_DIR}/src/core/rules-content-manifest.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/package-catalog.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/runtime-paths.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/rules-content-manifest.h")
qt_add_resources(qsanguosha_android_content_store_tests android_content_fixture
    PREFIX "/assets" BASE "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}"
    FILES
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/base-descriptor.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-inventory.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/base-script.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/bundled-addon.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/addon-translation.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-config.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-sanguosha.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-sgs-ex.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/core-ai.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-image.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-audio.fixture"
        "${QSAN_ANDROID_CONTENT_FIXTURE_ROOT}/media-font.fixture")
qt_add_resources(qsanguosha_android_content_store_tests android_content_revision_fixture
    PREFIX "/"
    FILES "${qsan_android_revision_fixture}")

# Register this resource only for the bundled legacy-to-modular migration cases.
set(qsan_modular_fixture "${CMAKE_CURRENT_BINARY_DIR}/generated/android-modular-fixture")
file(MAKE_DIRECTORY "${qsan_modular_fixture}")
set(qsan_modular_body "return true")
string(SHA256 qsan_modular_hash "${qsan_modular_body}")
string(LENGTH "${qsan_modular_body}" qsan_modular_size)
file(WRITE "${qsan_modular_fixture}/base.lua" "${qsan_modular_body}")
file(WRITE "${qsan_modular_fixture}/manifest.json"
    "{\"schema_version\":1,\"id\":\"base\",\"version\":\"1.0.0\",\"engine_api\":1,\"dependencies\":[],\"assets\":{},\"extensions\":[{\"name\":\"base\",\"script\":\"lua/base.lua\",\"dependencies\":[],\"libs\":[],\"lang\":[],\"ai\":[]}],\"files\":[{\"path\":\"lua/base.lua\",\"role\":\"rules\",\"size\":${qsan_modular_size},\"sha256\":\"${qsan_modular_hash}\"}]}")
set_source_files_properties("${qsan_modular_fixture}/base.lua" PROPERTIES QT_RESOURCE_ALIAS "packages/base/lua/base.lua")
set_source_files_properties("${qsan_modular_fixture}/manifest.json" PROPERTIES QT_RESOURCE_ALIAS "packages/base/manifest.json")
qt_add_resources(qsanguosha_android_content_store_tests android_modular_migration_fixture
    PREFIX "/assets" FILES "${qsan_modular_fixture}/base.lua" "${qsan_modular_fixture}/manifest.json")
target_include_directories(qsanguosha_android_content_store_tests PRIVATE "${CMAKE_SOURCE_DIR}/src/core")
target_link_libraries(qsanguosha_android_content_store_tests PRIVATE Qt6::Core ${qsan_content_zlib})
set_target_properties(qsanguosha_android_content_store_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(qsanguosha_android_content_store_tests PRIVATE /utf-8)
endif()
qsan_add_ctest(qsanguosha_android_content_store qsanguosha_android_content_store_tests
    LABELS "android;content;fast" TIMEOUT 60)
