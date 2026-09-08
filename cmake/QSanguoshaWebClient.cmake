include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/QSanguoshaRulesSession.cmake")

# The production Worker and native probe link the same session/lifecycle code.
add_executable(qsanguosha_client_wasm src/client/runtime/client-rules-wasm.cpp)
target_link_libraries(qsanguosha_client_wasm PRIVATE
    qsanguosha_rules_session
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
