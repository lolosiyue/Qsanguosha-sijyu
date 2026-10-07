# Regenerated before each GUI build, without changing timestamps if unchanged.
# A version alone cannot safely order debug builds against GitHub release tags.
find_program(QSAN_UPDATE_GIT git)
set(commit "")
set(clean 0)
if(QSAN_UPDATE_GIT)
    execute_process(COMMAND "${QSAN_UPDATE_GIT}" rev-parse HEAD
        WORKING_DIRECTORY "${ROOT}" OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE head_result ERROR_QUIET)
    execute_process(COMMAND "${QSAN_UPDATE_GIT}" status --porcelain --untracked-files=normal
        WORKING_DIRECTORY "${ROOT}" OUTPUT_VARIABLE dirty RESULT_VARIABLE status_result ERROR_QUIET)
    if(head_result EQUAL 0 AND status_result EQUAL 0 AND dirty STREQUAL "")
        set(clean 1)
    endif()
endif()
if(NOT commit MATCHES "^[0-9a-f]+$" OR NOT head_result EQUAL 0)
    set(commit "")
endif()
file(WRITE "${OUTPUT}.in"
    "#pragma once\n#define QSAN_UPDATE_COMMIT \"${commit}\"\n#define QSAN_UPDATE_CLEAN_BUILD ${clean}\n")
configure_file("${OUTPUT}.in" "${OUTPUT}" COPYONLY)
