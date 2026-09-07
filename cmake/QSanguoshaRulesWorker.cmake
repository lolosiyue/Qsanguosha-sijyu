include_guard(GLOBAL)

# This is an explicitly requested verification target in the existing WASM-only
# build, not a new Web product. Node's target and default builds are unchanged.
# Clone its source/link recipe so bootstrap assets, exports, exception policy
# and package registrars cannot acquire an independent browser implementation.
get_target_property(qsan_worker_sources qsanguosha_rules_fixture_wasm SOURCES)
get_target_property(qsan_worker_definitions qsanguosha_rules_fixture_wasm COMPILE_DEFINITIONS)
get_target_property(qsan_worker_links qsanguosha_rules_fixture_wasm LINK_LIBRARIES)
get_target_property(qsan_worker_options qsanguosha_rules_fixture_wasm LINK_OPTIONS)
get_target_property(qsan_worker_dependencies qsanguosha_rules_fixture_wasm LINK_DEPENDS)
if(NOT "-sENVIRONMENT=node" IN_LIST qsan_worker_options)
    message(FATAL_ERROR "The WASM fixture host recipe changed; audit Worker options")
endif()
list(REMOVE_ITEM qsan_worker_options "-sENVIRONMENT=node")
list(APPEND qsan_worker_options "-sENVIRONMENT=worker")
add_executable(qsanguosha_rules_fixture_worker EXCLUDE_FROM_ALL ${qsan_worker_sources})
target_compile_definitions(qsanguosha_rules_fixture_worker PRIVATE ${qsan_worker_definitions})
target_compile_options(qsanguosha_rules_fixture_worker PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fexceptions>")
target_link_libraries(qsanguosha_rules_fixture_worker PRIVATE ${qsan_worker_links})
target_link_options(qsanguosha_rules_fixture_worker PRIVATE ${qsan_worker_options})
set_target_properties(qsanguosha_rules_fixture_worker PROPERTIES
    FOLDER "Tests" SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rules-wasm/$<CONFIG>"
    LINK_DEPENDS "${qsan_worker_dependencies}"
)
add_custom_command(TARGET qsanguosha_rules_fixture_worker POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${qsan_wasm_assets}/fixture-assets.json"
        "$<TARGET_FILE_DIR:qsanguosha_rules_fixture_worker>/qsanguosha_rules_fixture_worker.assets.json"
    VERBATIM
)
