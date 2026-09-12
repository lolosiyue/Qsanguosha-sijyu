include_guard(GLOBAL)

if(NOT EMSCRIPTEN OR NOT QT_FEATURE_thread)
    message(FATAL_ERROR "Browser single-player requires Qt 6.11.1 wasm_multithread, not wasm_singlethread")
endif()

add_executable(qsanguosha_solo_wasm
    src/client/runtime/solo-server-host.cpp
    src/client/runtime/solo-server-host.h
    src/client/runtime/solo-server-wasm.cpp
)
target_compile_definitions(qsanguosha_solo_wasm PRIVATE
    QSAN_ENGINE_BUILD QSAN_SERVER_CORE_ONLY)
target_link_libraries(qsanguosha_solo_wasm PRIVATE
    qsanguosha_client_runtime
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
)
set_target_properties(qsanguosha_solo_wasm PROPERTIES
    FOLDER "Products" SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/web-solo/$<CONFIG>"
)
qsan_configure_wasm_rules_module(qsanguosha_solo_wasm createQSanguoshaSolo worker
    "['_qsan_solo_initialize','_qsan_solo_start','_qsan_solo_frame','_qsan_solo_pump','_qsan_solo_stop']" CONTENT_FREE)
# Room initialization, Room and RoomThread need already-started workers before
# any synchronous Qt wait. Never run this runtime on the browser's UI thread.
target_link_options(qsanguosha_solo_wasm PRIVATE
    -sPTHREAD_POOL_SIZE=8
    -sPTHREAD_POOL_SIZE_STRICT=2
)
add_custom_command(TARGET qsanguosha_solo_wasm POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/package-web-solo.py"
        --seal-runtime --solo-module "$<TARGET_FILE:qsanguosha_solo_wasm>"
    VERBATIM
)
set_property(TARGET qsanguosha_solo_wasm APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/package-web-solo.py")
