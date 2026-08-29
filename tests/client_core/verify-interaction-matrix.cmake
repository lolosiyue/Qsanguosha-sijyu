if(NOT DEFINED MATRIX_FILE OR NOT EXISTS "${MATRIX_FILE}")
    message(FATAL_ERROR "interaction matrix is missing: ${MATRIX_FILE}")
endif()
if(NOT DEFINED GENERATOR_EXE OR NOT EXISTS "${GENERATOR_EXE}")
    message(FATAL_ERROR "interaction inventory generator is missing: ${GENERATOR_EXE}")
endif()
if(NOT DEFINED GENERATED_FILE)
    message(FATAL_ERROR "GENERATED_FILE is required")
endif()

execute_process(
    COMMAND "${GENERATOR_EXE}" --interaction-inventory "${GENERATED_FILE}"
    RESULT_VARIABLE generator_result
    OUTPUT_VARIABLE generator_output
    ERROR_VARIABLE generator_error
)
if(NOT generator_result EQUAL 0)
    message(FATAL_ERROR
        "interaction inventory generation failed (${generator_result}): ${generator_error}${generator_output}")
endif()

file(READ "${GENERATED_FILE}" generated_json)
file(READ "${MATRIX_FILE}" committed_json)
foreach(field schema_version total_commands canonical_typed legacy_adapter
        missing_builder missing_presenter missing_validator missing_reply_encoder
        presenter_invocations presented_types)
    string(JSON ${field} GET "${generated_json}" ${field})
endforeach()
string(JSON command_count LENGTH "${generated_json}" commands)

if(NOT schema_version EQUAL 2 OR NOT total_commands EQUAL 29
    OR NOT command_count EQUAL 29 OR NOT canonical_typed EQUAL 28
    OR NOT legacy_adapter EQUAL 1 OR NOT missing_builder EQUAL 0
    OR NOT missing_presenter EQUAL 0 OR NOT missing_validator EQUAL 0
    OR NOT missing_reply_encoder EQUAL 0 OR NOT presenter_invocations EQUAL 29
    OR NOT presented_types EQUAL 29)
    message(FATAL_ERROR
        "invalid generated interaction inventory: schema=${schema_version}, total=${total_commands}, entries=${command_count}, canonical=${canonical_typed}, legacy=${legacy_adapter}, missing=${missing_builder}/${missing_presenter}/${missing_validator}/${missing_reply_encoder}, presenters=${presenter_invocations}/${presented_types}")
endif()

if(NOT generated_json STREQUAL committed_json)
    message(FATAL_ERROR
        "committed interaction matrix is stale; regenerate it with ${GENERATOR_EXE} --interaction-inventory ${MATRIX_FILE}")
endif()

message(STATUS "Generated Client interaction matrix matches 28 canonical + 1 legacy adapter")
