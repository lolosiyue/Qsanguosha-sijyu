include_guard(GLOBAL)

# This product owns a persistent session, not the one-shot fixture evaluator.
# Keep its source closure out of native products until they opt into this API.
add_executable(qsanguosha_client_wasm
    src/client/runtime/client-rules-session.cpp
    src/client/runtime/client-rules-session.h
    src/client/runtime/client-rules-wasm.cpp
    src/client/interaction-reply-encoder.cpp
)
target_link_libraries(qsanguosha_client_wasm PRIVATE
    qsanguosha_client_runtime
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
)
set_target_properties(qsanguosha_client_wasm PROPERTIES
    FOLDER "Products"
    SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/web-wasm/$<CONFIG>"
)
qsan_configure_wasm_rules_module(qsanguosha_client_wasm
    createQSanguoshaClient worker
    "['_qsan_client_initialize','_qsan_client_evaluate','_qsan_client_shutdown']")
add_custom_command(TARGET qsanguosha_client_wasm POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${qsan_wasm_assets}/fixture-assets.json"
        "$<TARGET_FILE_DIR:qsanguosha_client_wasm>/qsanguosha_client_wasm.assets.json"
    VERBATIM
)

# Explicit packaging only: ordinary cross builds do not modify web/public.
add_custom_target(package-web-runtime
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/package-web-runtime.py"
        --module "$<TARGET_FILE:qsanguosha_client_wasm>"
        --destination "${CMAKE_CURRENT_SOURCE_DIR}/web/public/rules"
    DEPENDS qsanguosha_client_wasm
    VERBATIM
)
set_target_properties(package-web-runtime PROPERTIES FOLDER "Packaging")
