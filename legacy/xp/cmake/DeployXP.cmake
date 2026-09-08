foreach(required
        QSAN_CONFIG
        QSAN_EXECUTABLE
        QSAN_SERVER_EXECUTABLE
        QSAN_ASSET_ROOT
        QSAN_QT_ROOT
        QSAN_VC_REDIST_DIR
        QSAN_UCRT_REDIST_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

# Check the pair before mutating an existing deployment. A missing helper must
# never produce a superficially complete single-executable portable folder.
if(NOT EXISTS "${QSAN_SERVER_EXECUTABLE}")
    message(FATAL_ERROR "XP server executable is missing: ${QSAN_SERVER_EXECUTABLE}")
endif()

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
if(NOT "${qsan_output_dir}/QSanguoshaXPServer.exe" STREQUAL "${QSAN_SERVER_EXECUTABLE}")
    file(COPY_FILE "${QSAN_SERVER_EXECUTABLE}" "${qsan_output_dir}/QSanguoshaXPServer.exe" ONLY_IF_DIFFERENT)
endif()
# An interrupted deployment must not retain a previous completion manifest.
# Remove obsolete split manifests as well so one file remains authoritative.
file(REMOVE
    "${qsan_output_dir}/xp-payload-manifest.json"
    "${qsan_output_dir}/xp-payload-manifest.json.partial"
    "${qsan_output_dir}/xp-build-identity.txt"
    "${qsan_output_dir}/xp-executables.sha256"
    "${qsan_output_dir}/xp-executables.sha256.partial")
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

set(qsan_required_asset_directories lua lang qss skins image extensions)
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
            /XD .git
            /XF .stignore *.bak *.bak-* *.qml *.qmlc *.mp4 *.webm *.mkv
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
        "${qsan_output_dir}/${qsan_asset_dir}/desktop.ini"
        "${qsan_output_dir}/${qsan_asset_dir}/.stignore"
        "${qsan_output_dir}/${qsan_asset_dir}/*.bak"
        "${qsan_output_dir}/${qsan_asset_dir}/*.bak-*")
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

# Both programs expose a lightweight identity query before Engine/bootstrap.
# Verify pairing after DLL deployment, then publish the sole completion manifest last.
foreach(program QSanguoshaXP QSanguoshaXPServer)
    execute_process(COMMAND "${qsan_output_dir}/${program}.exe" --xp-build-id
        OUTPUT_VARIABLE ${program}_identity OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE identity_result TIMEOUT 10)
    if(NOT identity_result EQUAL 0 OR "${${program}_identity}" STREQUAL "")
        message(FATAL_ERROR "Cannot verify deployed ${program} identity (${identity_result})")
    endif()
endforeach()
if(NOT QSanguoshaXP_identity STREQUAL QSanguoshaXPServer_identity)
    message(FATAL_ERROR "XP GUI/server build identity mismatch")
endif()
execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_LIST_DIR}/../tools/write-xp-manifest.ps1"
    -PayloadRoot "${qsan_output_dir}"
    -BuildIdentity "${QSanguoshaXP_identity}"
    RESULT_VARIABLE manifest_result)
if(NOT manifest_result EQUAL 0)
    message(FATAL_ERROR "XP payload manifest generation failed (${manifest_result})")
endif()
message(STATUS "Portable XP ${QSAN_CONFIG} folder: ${qsan_output_dir}")
