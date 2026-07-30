foreach(required QSAN_CONFIG QSAN_EXECUTABLE QSAN_WINDEPLOYQT QSAN_QML_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(QSAN_CONFIG STREQUAL "Debug")
    set(deploy_configuration --debug)
else()
    set(deploy_configuration --release)
endif()

execute_process(
    COMMAND "${QSAN_WINDEPLOYQT}"
        "${deploy_configuration}"
        --compiler-runtime
        --qmldir "${QSAN_QML_DIR}"
        "${QSAN_EXECUTABLE}"
    RESULT_VARIABLE deploy_result
)
if(NOT deploy_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed with exit code ${deploy_result}")
endif()

if(QSAN_CONFIG STREQUAL "Release")
    if(NOT DEFINED QSAN_FMOD_RUNTIME OR NOT EXISTS "${QSAN_FMOD_RUNTIME}")
        message(FATAL_ERROR "Release deployment requires QSAN_FMOD_RUNTIME")
    endif()
    get_filename_component(output_dir "${QSAN_EXECUTABLE}" DIRECTORY)
    file(COPY_FILE "${QSAN_FMOD_RUNTIME}" "${output_dir}/fmodex64.dll" ONLY_IF_DIFFERENT)
endif()
