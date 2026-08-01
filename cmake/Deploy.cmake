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

# exe 未直接 import Qt6Multimedia.dll（僅由 QML Video 動態載入），windeployqt
# 依賴掃描不一定會部署 multimedia 後端 plugin。這裡明確從 Qt 安裝目錄複製
# plugins/multimedia/ 與 Qt6Multimedia*.dll，確保影片背景可播放。
get_filename_component(qt_bin_dir "${QSAN_WINDEPLOYQT}" DIRECTORY)
get_filename_component(qt_prefix "${qt_bin_dir}" DIRECTORY)
get_filename_component(output_dir "${QSAN_EXECUTABLE}" DIRECTORY)
set(qt_multimedia_dir "${qt_prefix}/plugins/multimedia")
if(EXISTS "${qt_multimedia_dir}")
    file(MAKE_DIRECTORY "${output_dir}/plugins/multimedia")
    file(GLOB qt_multimedia_plugins
        "${qt_multimedia_dir}/ffmpegmediaplugin*.dll"
        "${qt_multimedia_dir}/windowsmediaplugin*.dll")
    foreach(plugin ${qt_multimedia_plugins})
        get_filename_component(plugin_name "${plugin}" NAME)
        file(COPY_FILE "${plugin}" "${output_dir}/plugins/multimedia/${plugin_name}"
            ONLY_IF_DIFFERENT)
    endforeach()
endif()
if(QSAN_CONFIG STREQUAL "Debug")
    set(qt_multimedia_dll "${qt_bin_dir}/Qt6Multimediad.dll")
else()
    set(qt_multimedia_dll "${qt_bin_dir}/Qt6Multimedia.dll")
endif()
if(EXISTS "${qt_multimedia_dll}")
    get_filename_component(qt_multimedia_dll_name "${qt_multimedia_dll}" NAME)
    file(COPY_FILE "${qt_multimedia_dll}" "${output_dir}/${qt_multimedia_dll_name}"
        ONLY_IF_DIFFERENT)
endif()

if(QSAN_CONFIG STREQUAL "Release")
    if(NOT DEFINED QSAN_FMOD_RUNTIME OR NOT EXISTS "${QSAN_FMOD_RUNTIME}")
        message(FATAL_ERROR "Release deployment requires QSAN_FMOD_RUNTIME")
    endif()
    get_filename_component(output_dir "${QSAN_EXECUTABLE}" DIRECTORY)
    file(COPY_FILE "${QSAN_FMOD_RUNTIME}" "${output_dir}/fmodex64.dll" ONLY_IF_DIFFERENT)
endif()
