if(NOT EXISTS "${QSAN_TUI_EXECUTABLE}")
    message(FATAL_ERROR "TUI executable does not exist: ${QSAN_TUI_EXECUTABLE}")
endif()

if(WIN32)
    if(NOT EXISTS "${QSAN_DUMPBIN}")
        message(FATAL_ERROR "dumpbin is required for the TUI dependency gate")
    endif()
    execute_process(
        COMMAND "${QSAN_DUMPBIN}" /DEPENDENTS "${QSAN_TUI_EXECUTABLE}"
        RESULT_VARIABLE qsan_dependency_result
        OUTPUT_VARIABLE qsan_dependencies
        ERROR_VARIABLE qsan_dependency_error
    )
else()
    find_program(QSAN_LDD ldd REQUIRED)
    execute_process(
        COMMAND "${QSAN_LDD}" "${QSAN_TUI_EXECUTABLE}"
        RESULT_VARIABLE qsan_dependency_result
        OUTPUT_VARIABLE qsan_dependencies
        ERROR_VARIABLE qsan_dependency_error
    )
endif()

if(NOT qsan_dependency_result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect TUI dependencies: ${qsan_dependency_error}")
endif()

string(TOLOWER "${qsan_dependencies}" qsan_dependencies_lower)
foreach(qsan_forbidden_dependency
        qt6gui qt6widgets qt6quick qt6qml qt6multimedia qt6opengl qt6websockets
        fmod recorder replayer)
    if(qsan_dependencies_lower MATCHES "${qsan_forbidden_dependency}")
        message(FATAL_ERROR
            "qsanguosha_tui imports forbidden dependency: ${qsan_forbidden_dependency}")
    endif()
endforeach()

message(STATUS "qsanguosha_tui dependency gate passed")
