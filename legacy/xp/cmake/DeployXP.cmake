foreach(required
        QSAN_CONFIG
        QSAN_EXECUTABLE
        QSAN_ASSET_ROOT
        QSAN_QT_ROOT
        QSAN_VC_REDIST_DIR
        QSAN_UCRT_REDIST_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(QSAN_CONFIG STREQUAL "Debug")
    set(qsan_debug_suffix d)
    set(qsan_fmod_runtime "${QSAN_FMOD_RUNTIME_DEBUG}")
    set(qsan_fmod_name fmodexL.dll)
else()
    set(qsan_debug_suffix "")
    set(qsan_fmod_runtime "${QSAN_FMOD_RUNTIME_RELEASE}")
    set(qsan_fmod_name fmodex.dll)
endif()

if(NOT EXISTS "${QSAN_EXECUTABLE}")
    message(FATAL_ERROR "XP executable not found: ${QSAN_EXECUTABLE}")
endif()
if(NOT EXISTS "${qsan_fmod_runtime}")
    message(FATAL_ERROR
        "XP ${QSAN_CONFIG} deployment requires an x86 ${qsan_fmod_name}; "
        "set QSAN_XP_FMOD_RUNTIME_${QSAN_CONFIG}")
endif()
get_filename_component(qsan_fmod_source_name "${qsan_fmod_runtime}" NAME)
if(NOT qsan_fmod_source_name STREQUAL qsan_fmod_name)
    message(FATAL_ERROR
        "XP ${QSAN_CONFIG} FMOD runtime must be named ${qsan_fmod_name} "
        "(got ${qsan_fmod_source_name})")
endif()

get_filename_component(qsan_build_output_dir "${QSAN_EXECUTABLE}" DIRECTORY)
if(DEFINED QSAN_DEPLOY_ROOT AND NOT "${QSAN_DEPLOY_ROOT}" STREQUAL "")
    set(qsan_output_dir "${QSAN_DEPLOY_ROOT}")
    file(MAKE_DIRECTORY "${qsan_output_dir}")
    get_filename_component(qsan_executable_name "${QSAN_EXECUTABLE}" NAME)
    file(COPY_FILE "${QSAN_EXECUTABLE}"
        "${qsan_output_dir}/${qsan_executable_name}" ONLY_IF_DIFFERENT)
else()
    set(qsan_output_dir "${qsan_build_output_dir}")
endif()
set(qsan_qt_libraries Core Gui Network Widgets)
foreach(qsan_qt_library IN LISTS qsan_qt_libraries)
    set(qsan_qt_dll
        "${QSAN_QT_ROOT}/bin/Qt5${qsan_qt_library}${qsan_debug_suffix}.dll")
    if(NOT EXISTS "${qsan_qt_dll}")
        message(FATAL_ERROR "Required XP Qt DLL is missing: ${qsan_qt_dll}")
    endif()
    file(COPY_FILE "${qsan_qt_dll}"
        "${qsan_output_dir}/Qt5${qsan_qt_library}${qsan_debug_suffix}.dll"
        ONLY_IF_DIFFERENT)
endforeach()

set(qsan_qt_plugins
    "platforms/qwindows${qsan_debug_suffix}.dll"
    "imageformats/qgif${qsan_debug_suffix}.dll"
    "imageformats/qico${qsan_debug_suffix}.dll"
    "imageformats/qjpeg${qsan_debug_suffix}.dll"
    "bearer/qgenericbearer${qsan_debug_suffix}.dll"
    "bearer/qnativewifibearer${qsan_debug_suffix}.dll")
foreach(qsan_qt_plugin IN LISTS qsan_qt_plugins)
    set(qsan_qt_plugin_source "${QSAN_QT_ROOT}/plugins/${qsan_qt_plugin}")
    if(NOT EXISTS "${qsan_qt_plugin_source}")
        message(FATAL_ERROR "Required XP Qt plugin is missing: ${qsan_qt_plugin_source}")
    endif()
    get_filename_component(qsan_qt_plugin_dir "${qsan_qt_plugin}" DIRECTORY)
    file(MAKE_DIRECTORY "${qsan_output_dir}/${qsan_qt_plugin_dir}")
    file(COPY_FILE "${qsan_qt_plugin_source}"
        "${qsan_output_dir}/${qsan_qt_plugin}" ONLY_IF_DIFFERENT)
endforeach()

# Remove the nested layout from early PoC deployments. XP's inbox EXPAND.EXE
# does not reliably restore that extra CAB directory level; the XP entry point
# supplies the root platform path before QApplication is constructed.
file(REMOVE_RECURSE "${qsan_output_dir}/plugins")

foreach(qsan_runtime_dir QSAN_VC_REDIST_DIR QSAN_UCRT_REDIST_DIR)
    if(NOT IS_DIRECTORY "${${qsan_runtime_dir}}")
        message(FATAL_ERROR "XP runtime directory is missing: ${${qsan_runtime_dir}}")
    endif()
    file(GLOB qsan_runtime_dlls "${${qsan_runtime_dir}}/*.dll")
    if(NOT qsan_runtime_dlls)
        message(FATAL_ERROR "XP runtime directory has no DLLs: ${${qsan_runtime_dir}}")
    endif()
    foreach(qsan_runtime_dll IN LISTS qsan_runtime_dlls)
        get_filename_component(qsan_runtime_name "${qsan_runtime_dll}" NAME)
        file(COPY_FILE "${qsan_runtime_dll}"
            "${qsan_output_dir}/${qsan_runtime_name}" ONLY_IF_DIFFERENT)
    endforeach()
endforeach()

file(COPY_FILE "${qsan_fmod_runtime}" "${qsan_output_dir}/${qsan_fmod_name}"
    ONLY_IF_DIFFERENT)
file(WRITE "${qsan_output_dir}/qt.conf" "[Paths]\nPlugins=.\n")

set(qsan_required_asset_directories lua lang qss skins image)
# Third-party extensions are outside the XP compatibility promise and may use
# newer Lua syntax. Keep the legacy bundle limited to the supported core data.
file(REMOVE_RECURSE "${qsan_output_dir}/extensions")
set(qsan_optional_asset_directories audio etc listserver)
find_program(QSAN_ROBOCOPY robocopy REQUIRED)
function(qsan_copy_asset_directory qsan_asset_dir qsan_required)
    set(qsan_asset_source "${QSAN_ASSET_ROOT}/${qsan_asset_dir}")
    if(NOT IS_DIRECTORY "${qsan_asset_source}")
        if(NOT qsan_required)
            return()
        endif()
        message(FATAL_ERROR
            "Required XP asset directory is missing: ${qsan_asset_source}")
    endif()

    execute_process(
        COMMAND "${QSAN_ROBOCOPY}"
            "${qsan_asset_source}"
            "${qsan_output_dir}/${qsan_asset_dir}"
            /MIR /COPY:DAT /DCOPY:DAT /R:1 /W:1 /MT:8
            /NFL /NDL /NJH /NJS /NP
            /XF *.qml *.qmlc *.mp4 *.webm *.mkv
        RESULT_VARIABLE qsan_robocopy_result
    )
    if(qsan_robocopy_result GREATER_EQUAL 8)
        message(FATAL_ERROR
            "Failed to copy XP asset directory ${qsan_asset_dir}: "
            "robocopy exit code ${qsan_robocopy_result}")
    endif()
    file(GLOB_RECURSE qsan_excluded_assets LIST_DIRECTORIES FALSE
        "${qsan_output_dir}/${qsan_asset_dir}/*.qml"
        "${qsan_output_dir}/${qsan_asset_dir}/*.qmlc"
        "${qsan_output_dir}/${qsan_asset_dir}/*.mp4"
        "${qsan_output_dir}/${qsan_asset_dir}/*.webm"
        "${qsan_output_dir}/${qsan_asset_dir}/*.mkv"
        "${qsan_output_dir}/${qsan_asset_dir}/Thumbs.db"
        "${qsan_output_dir}/${qsan_asset_dir}/desktop.ini")
    if(qsan_excluded_assets)
        file(REMOVE ${qsan_excluded_assets})
    endif()
endfunction()

foreach(qsan_asset_dir IN LISTS qsan_required_asset_directories)
    qsan_copy_asset_directory("${qsan_asset_dir}" TRUE)
endforeach()
foreach(qsan_asset_dir IN LISTS qsan_optional_asset_directories)
    qsan_copy_asset_directory("${qsan_asset_dir}" FALSE)
endforeach()
foreach(qsan_asset_file qt_zh_CN.qm sanguosha.qm assets-manifest.json)
    if(EXISTS "${QSAN_ASSET_ROOT}/${qsan_asset_file}")
        file(COPY_FILE "${QSAN_ASSET_ROOT}/${qsan_asset_file}"
            "${qsan_output_dir}/${qsan_asset_file}" ONLY_IF_DIFFERENT)
    endif()
endforeach()

foreach(qsan_helper_file AUTORUN.INF INSTALL-XP.CMD ACCEPTANCE-XP.CMD)
    file(COPY_FILE
        "${CMAKE_CURRENT_LIST_DIR}/../assets/${qsan_helper_file}"
        "${qsan_output_dir}/${qsan_helper_file}" ONLY_IF_DIFFERENT)
endforeach()

message(STATUS "Portable XP ${QSAN_CONFIG} folder: ${qsan_output_dir}")
