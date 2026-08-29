if(NOT DEFINED MATRIX_FILE OR NOT EXISTS "${MATRIX_FILE}")
    message(FATAL_ERROR "interaction matrix is missing: ${MATRIX_FILE}")
endif()

file(READ "${MATRIX_FILE}" matrix_json)
string(JSON schema_version GET "${matrix_json}" schema_version)
string(JSON total_commands GET "${matrix_json}" total_commands)
string(JSON command_count LENGTH "${matrix_json}" commands)
string(JSON passthrough_count GET "${matrix_json}" remaining_builtin_passthrough)

if(NOT schema_version EQUAL 1 OR NOT total_commands EQUAL 29
    OR NOT command_count EQUAL 29 OR NOT passthrough_count EQUAL 0)
    message(FATAL_ERROR
        "invalid interaction matrix summary: schema=${schema_version}, total=${total_commands}, entries=${command_count}, passthrough=${passthrough_count}")
endif()

math(EXPR last_command "${command_count} - 1")
foreach(index RANGE 0 ${last_command})
    foreach(field command type builder validator desktop_renderer test passthrough)
        string(JSON value ERROR_VARIABLE json_error GET "${matrix_json}" commands ${index} ${field})
        if(json_error)
            message(FATAL_ERROR "interaction matrix entry ${index} is missing ${field}: ${json_error}")
        endif()
        if(NOT field STREQUAL "passthrough" AND value STREQUAL "")
            message(FATAL_ERROR "interaction matrix entry ${index} has an empty ${field}")
        endif()
    endforeach()
    string(JSON passthrough GET "${matrix_json}" commands ${index} passthrough)
    if(passthrough)
        message(FATAL_ERROR "built-in interaction ${index} is marked passthrough")
    endif()
endforeach()

message(STATUS "Client interaction matrix gate passed for 29 commands")
