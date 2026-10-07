cmake_minimum_required(VERSION 3.20)
project(QsanUpdateCatalogTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Widgets Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(ZLIB REQUIRED)

enable_testing()
add_executable(update-catalog-test
    ../update-catalog-test.cpp
    ../../src/core/update-catalog.cpp
)
target_include_directories(update-catalog-test PRIVATE ../../src/core)
target_link_libraries(update-catalog-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME update-catalog-contract COMMAND update-catalog-test)

add_executable(update-transfer-test
    ../update-transfer-test.cpp
    ../update-network-fixture.h
    ../../src/core/update-transfer.cpp
    ../../src/core/update-transfer.h
    ../../src/core/update-catalog.cpp
)
target_include_directories(update-transfer-test PRIVATE ../../src/core)
target_link_libraries(update-transfer-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME update-transfer-contract COMMAND update-transfer-test)

set(update_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
add_custom_target(update-test-fixtures
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make-fixtures.py ${update_fixture_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${update_fixture_dir}/catalog-input
    COMMAND ${CMAKE_COMMAND} -E copy ${update_fixture_dir}/v1.zip ${update_fixture_dir}/catalog-input/v1.zip
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/generate-update-manifest.py
        --input-dir ${update_fixture_dir}/catalog-input --output ${update_fixture_dir}/generated-catalog.json
        --base-url https://assets.example.invalid/packages/ --game-version 20251231
    BYPRODUCTS ${update_fixture_dir}/v1-wrapped.zip ${update_fixture_dir}/v1.zip ${update_fixture_dir}/v2.zip
               ${update_fixture_dir}/bad-hash.zip ${update_fixture_dir}/traversal.zip
               ${update_fixture_dir}/symlink.zip ${update_fixture_dir}/compression-bomb.zip
    VERBATIM
)
add_executable(update-store-test
    ../update-store-test.cpp
    ../../src/core/package-store.cpp
    ../../src/core/package-catalog.cpp
    ../../src/core/runtime-paths.cpp
    ../../src/core/android-zip-reader.cpp
)
target_include_directories(update-store-test PRIVATE ../../src/core)
target_link_libraries(update-store-test PRIVATE Qt6::Core ZLIB::ZLIB)
add_dependencies(update-store-test update-test-fixtures)
add_test(NAME update-store-contract COMMAND update-store-test ${update_fixture_dir})

add_executable(update-storage-test
    ../update-storage-test.cpp
    ../../src/core/package-store.cpp
    ../../src/core/package-catalog.cpp
    ../../src/core/runtime-paths.cpp
    ../../src/core/android-zip-reader.cpp
)
target_include_directories(update-storage-test PRIVATE ../../src/core)
target_link_libraries(update-storage-test PRIVATE Qt6::Core ZLIB::ZLIB)
add_test(NAME update-storage-budget-contract COMMAND update-storage-test)

add_test(NAME update-manifest-generator-contract
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/packages/tests/test_generate_update_manifest.py
)

add_executable(update-dialog-test
    ../update-dialog-test.cpp
    ../update-network-fixture.h
    ../../src/dialog/update-dialog.cpp
    ../../src/dialog/update-dialog.h
    ../../src/core/update-transfer.cpp
    ../../src/core/update-transfer.h
    ../../src/core/update-catalog.cpp
    ../../src/core/package-store.cpp
    ../../src/core/package-catalog.cpp
    ../../src/core/runtime-paths.cpp
    ../../src/core/android-zip-reader.cpp
)
target_include_directories(update-dialog-test PRIVATE ../../src/core ../../src/dialog)
target_link_libraries(update-dialog-test PRIVATE Qt6::Core Qt6::Network Qt6::Widgets Qt6::Test ZLIB::ZLIB)
add_dependencies(update-dialog-test update-test-fixtures)
add_test(NAME update-dialog-contract COMMAND update-dialog-test ${update_fixture_dir})
set_tests_properties(update-dialog-contract PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_dependencies(update-catalog-test update-test-fixtures)
add_test(NAME update-generated-catalog-contract
    COMMAND update-catalog-test ${update_fixture_dir}/generated-catalog.json)
