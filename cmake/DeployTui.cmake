foreach(required QSAN_CONFIG QSAN_EXECUTABLE QSAN_WINDEPLOYQT QSAN_DUMPBIN
        QSAN_VS_INSTALL_DIR QSAN_OUTPUT_DIR QSAN_ASSET_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

# The target owns this generated package directory. Recreate it so removed
# GUI/presentation assets cannot survive from an older deployment.
file(REMOVE_RECURSE "${QSAN_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${QSAN_OUTPUT_DIR}")
get_filename_component(qsan_tui_name "${QSAN_EXECUTABLE}" NAME)
set(qsan_deployed_executable "${QSAN_OUTPUT_DIR}/${qsan_tui_name}")
file(COPY_FILE "${QSAN_EXECUTABLE}" "${qsan_deployed_executable}" ONLY_IF_DIFFERENT)

if(QSAN_CONFIG STREQUAL "Debug")
    set(qsan_deploy_configuration --debug)
else()
    set(qsan_deploy_configuration --release)
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "VCINSTALLDIR=${QSAN_VS_INSTALL_DIR}/VC"
        "${QSAN_WINDEPLOYQT}" "${qsan_deploy_configuration}"
        --compiler-runtime --no-translations "${qsan_deployed_executable}"
    RESULT_VARIABLE qsan_deploy_result
)
if(NOT qsan_deploy_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed with exit code ${qsan_deploy_result}")
endif()
file(GLOB qsan_compiler_runtime
    "${QSAN_OUTPUT_DIR}/msvcp*.dll"
    "${QSAN_OUTPUT_DIR}/vcruntime*.dll")
file(GLOB qsan_vc_redist "${QSAN_OUTPUT_DIR}/vc_redist*.exe")
if(NOT qsan_compiler_runtime AND NOT qsan_vc_redist)
    message(FATAL_ERROR "TUI package is missing the MSVC compiler runtime")
endif()

file(COPY "${QSAN_ASSET_ROOT}/lua" DESTINATION "${QSAN_OUTPUT_DIR}"
    PATTERN "qss" EXCLUDE)
file(COPY "${QSAN_ASSET_ROOT}/lang" DESTINATION "${QSAN_OUTPUT_DIR}"
    PATTERN "Audio" EXCLUDE)
if(EXISTS "${QSAN_ASSET_ROOT}/extensions")
    file(COPY "${QSAN_ASSET_ROOT}/extensions" DESTINATION "${QSAN_OUTPUT_DIR}")
endif()
file(COPY_FILE "${QSAN_ASSET_ROOT}/LICENSE" "${QSAN_OUTPUT_DIR}/LICENSE"
    ONLY_IF_DIFFERENT)

foreach(qsan_forbidden_directory image audio video qml replay
        lua/qss lang/zh_CN/Audio)
    if(EXISTS "${QSAN_OUTPUT_DIR}/${qsan_forbidden_directory}")
        message(FATAL_ERROR
            "TUI package contains forbidden path: ${qsan_forbidden_directory}")
    endif()
endforeach()
file(GLOB_RECURSE qsan_package_files RELATIVE "${QSAN_OUTPUT_DIR}"
    "${QSAN_OUTPUT_DIR}/*")
foreach(qsan_package_file IN LISTS qsan_package_files)
    string(TOLOWER "${qsan_package_file}" qsan_package_file_lower)
    if(qsan_package_file_lower MATCHES
            "(qt6gui|qt6widgets|qt6quick|qt6qml|qt6multimedia|fmod|replayer|recorder)")
        message(FATAL_ERROR
            "TUI package contains forbidden dependency or replay file: ${qsan_package_file}")
    endif()
endforeach()

foreach(qsan_smoke_argument --help --version)
    execute_process(
        COMMAND "${qsan_deployed_executable}" "${qsan_smoke_argument}"
        RESULT_VARIABLE qsan_smoke_result
        OUTPUT_QUIET
        ERROR_QUIET
        TIMEOUT 10
    )
    if(NOT qsan_smoke_result EQUAL 0)
        message(FATAL_ERROR
            "Packaged qsanguosha_tui ${qsan_smoke_argument} failed: ${qsan_smoke_result}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DQSAN_TUI_EXECUTABLE=${qsan_deployed_executable}"
        "-DQSAN_DUMPBIN=${QSAN_DUMPBIN}"
        -P "${QSAN_ASSET_ROOT}/cmake/VerifyTuiDependencies.cmake"
    RESULT_VARIABLE qsan_dependency_gate_result
)
if(NOT qsan_dependency_gate_result EQUAL 0)
    message(FATAL_ERROR "Packaged TUI dependency gate failed")
endif()

message(STATUS "qsanguosha_tui package smoke passed")
