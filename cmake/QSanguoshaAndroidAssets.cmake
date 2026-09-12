# Android runtime data bundled in the APK and copied to a writable root at startup.
function(qsan_add_android_runtime_assets target)
    if(NOT ANDROID)
        return()
    endif()

    set(qsan_android_root "${CMAKE_CURRENT_SOURCE_DIR}")
    set(qsan_android_files)

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(qsan_android_descriptor "${CMAKE_CURRENT_BINARY_DIR}/generated/android/runtime-content-base.json")
    set_source_files_properties("${qsan_android_descriptor}" PROPERTIES
        GENERATED TRUE QT_RESOURCE_ALIAS "runtime-content-base.json")
    qt_add_resources(${target} qsan_android_descriptor
        PREFIX "/assets" FILES "${qsan_android_descriptor}")

    # A ZIP's own manifest proves integrity; this independent APK inventory
    # proves that it also contains the complete media set required by this build.
    file(GLOB_RECURSE qsan_android_media CONFIGURE_DEPENDS
        "${qsan_android_root}/image/*" "${qsan_android_root}/audio/*" "${qsan_android_root}/font/*")
    set(qsan_android_inventory "${CMAKE_CURRENT_BINARY_DIR}/generated/android/media-inventory.json")
    add_custom_command(OUTPUT "${qsan_android_inventory}"
        COMMAND "${Python3_EXECUTABLE}"
            "${qsan_android_root}/tools/android/create-media-package.py"
            "${qsan_android_root}" "${CMAKE_CURRENT_BINARY_DIR}/android-media.zip"
            --inventory-output "${qsan_android_inventory}"
        DEPENDS "${qsan_android_root}/tools/android/create-media-package.py" ${qsan_android_media}
        VERBATIM
    )
    set_source_files_properties("${qsan_android_inventory}" PROPERTIES
        GENERATED TRUE QT_RESOURCE_ALIAS "media-inventory.json")
    qt_add_resources(${target} qsan_android_media_inventory
        PREFIX "/assets" FILES "${qsan_android_inventory}")

    foreach(qsan_android_required_dir lua extensions lang)
        if(NOT IS_DIRECTORY "${qsan_android_root}/${qsan_android_required_dir}")
            message(FATAL_ERROR
                "Android runtime package requires '${qsan_android_required_dir}/'")
        endif()
    endforeach()

    file(GLOB_RECURSE qsan_android_lua CONFIGURE_DEPENDS
        "${qsan_android_root}/lua/*.lua"
        "${qsan_android_root}/extensions/*.lua"
        "${qsan_android_root}/lang/*.lua"
    )
    set(qsan_android_valid_lua_relative)
    set(qsan_android_valid_extensions)
    set(qsan_android_valid_ai)
    foreach(qsan_android_file IN LISTS qsan_android_lua)
        file(RELATIVE_PATH qsan_android_relative "${qsan_android_root}" "${qsan_android_file}")
        string(REPLACE "\\" "/" qsan_android_relative "${qsan_android_relative}")
        if(qsan_android_relative MATCHES "(^|/)(\\.git|\\.svn|\\.hg|data|tests|test|examples|logs|temp)(/|$)"
            OR qsan_android_relative MATCHES "(\\.bak([.-].*)?|\\.backup|\\.sync-conflict-)")
            continue()
        endif()
        list(APPEND qsan_android_files "${qsan_android_file}")
        list(APPEND qsan_android_valid_lua_relative "${qsan_android_relative}")
        if(qsan_android_relative MATCHES "^extensions/")
            list(APPEND qsan_android_valid_extensions "${qsan_android_file}")
        elseif(qsan_android_relative MATCHES "^lua/ai/")
            list(APPEND qsan_android_valid_ai "${qsan_android_file}")
        endif()
    endforeach()

    # Derive the declaration from exactly the filtered APK inputs. Otherwise
    # bundled support Lua silently makes declared-v2 admission unsupported.
    set(qsan_android_lua_manifest "${CMAKE_CURRENT_BINARY_DIR}/generated/android/bundled-lua.txt")
    string(JOIN "\n" qsan_android_lua_manifest_text ${qsan_android_valid_lua_relative})
    file(CONFIGURE OUTPUT "${qsan_android_lua_manifest}"
        CONTENT "${qsan_android_lua_manifest_text}\n" @ONLY)
    add_custom_command(OUTPUT "${qsan_android_descriptor}"
        COMMAND "${Python3_EXECUTABLE}"
            "${qsan_android_root}/tools/android/create-runtime-descriptor.py"
            "${qsan_android_root}" "${qsan_android_descriptor}" "${qsan_android_lua_manifest}"
        DEPENDS "${qsan_android_root}/tools/android/create-runtime-descriptor.py"
            "${qsan_android_root}/lua/config.lua" "${qsan_android_lua_manifest}"
        VERBATIM)

    list(FIND qsan_android_valid_lua_relative "lua/config.lua" qsan_android_config_index)
    list(FIND qsan_android_valid_lua_relative "lua/sanguosha.lua" qsan_android_sanguosha_index)
    list(FIND qsan_android_valid_lua_relative "lua/ai/smart-ai.lua" qsan_android_smart_ai_index)
    if(qsan_android_config_index EQUAL -1 OR qsan_android_sanguosha_index EQUAL -1
        OR qsan_android_smart_ai_index EQUAL -1)
        message(FATAL_ERROR
            "Android runtime package requires lua/config.lua, lua/sanguosha.lua, and "
            "lua/ai/smart-ai.lua")
    endif()
    if(NOT qsan_android_valid_extensions OR NOT qsan_android_valid_ai)
        message(FATAL_ERROR
            "Android runtime package requires non-empty extensions/ and lua/ai/ Lua sets")
    endif()

    # Keep textual client assets and the basic fonts below in the APK; full media stays external.
    file(GLOB_RECURSE qsan_android_text CONFIGURE_DEPENDS
        "${qsan_android_root}/scenarios/*.html"
        "${qsan_android_root}/skins/*.json"
        "${qsan_android_root}/qss/*.qss"
        "${qsan_android_root}/ui-script/*.qml"
    )
    foreach(qsan_android_file IN LISTS qsan_android_text)
        file(RELATIVE_PATH qsan_android_relative "${qsan_android_root}" "${qsan_android_file}")
        string(REPLACE "\\" "/" qsan_android_relative "${qsan_android_relative}")
        if(qsan_android_relative MATCHES "(^|/)(\\.git|\\.svn|\\.hg|data|tests|test|examples|logs|temp)(/|$)"
            OR qsan_android_relative MATCHES "(\\.bak([.-].*)?|\\.backup|\\.sync-conflict-)")
            continue()
        endif()
        list(APPEND qsan_android_files "${qsan_android_file}")
    endforeach()

    # Settings::init uses simli by default; retain a CJK fallback in the base APK.
    foreach(qsan_android_font font/simli.ttf font/DroidSansFallback.ttf)
        if(NOT EXISTS "${qsan_android_root}/${qsan_android_font}")
            message(FATAL_ERROR "Android runtime package requires '${qsan_android_font}'")
        endif()
        list(APPEND qsan_android_files "${qsan_android_root}/${qsan_android_font}")
    endforeach()

    # Android QSoundEffect needs PCM WAV for the four latency-sensitive UI
    # effects; keep these small derived assets in the base APK. The original
    # OGG media remains external and is still the fallback at runtime.
    foreach(qsan_android_ui_effect button-down button-hover choose-item pop-up)
        set(qsan_android_ui_effect_file
            "${qsan_android_root}/resource/android/${qsan_android_ui_effect}.wav")
        if(NOT EXISTS "${qsan_android_ui_effect_file}")
            message(FATAL_ERROR
                "Android runtime package requires '${qsan_android_ui_effect_file}'")
        endif()
        list(APPEND qsan_android_files "${qsan_android_ui_effect_file}")
    endforeach()

    list(REMOVE_DUPLICATES qsan_android_files)

    # Hash the final APK resource set using stable resource aliases. Generated
    # descriptors are inputs too, while their build-directory paths stay out
    # of the identity.
    set(qsan_android_revision "${CMAKE_CURRENT_BINARY_DIR}/generated/android/android-content-revision.txt")
    set(qsan_android_revision_inputs
        "${CMAKE_CURRENT_BINARY_DIR}/generated/android/android-content-revision-inputs.txt")
    set(qsan_android_revision_lines)
    foreach(qsan_android_file IN LISTS qsan_android_files)
        file(RELATIVE_PATH qsan_android_relative "${qsan_android_root}" "${qsan_android_file}")
        string(REPLACE "\\" "/" qsan_android_relative "${qsan_android_relative}")
        list(APPEND qsan_android_revision_lines
            "assets/${qsan_android_relative}\t${qsan_android_file}")
    endforeach()
    list(APPEND qsan_android_revision_lines
        "assets/runtime-content-base.json\t${qsan_android_descriptor}"
        "assets/media-inventory.json\t${qsan_android_inventory}")
    string(JOIN "\n" qsan_android_revision_text ${qsan_android_revision_lines})
    file(CONFIGURE OUTPUT "${qsan_android_revision_inputs}"
        CONTENT "${qsan_android_revision_text}\n" @ONLY)
    add_custom_command(OUTPUT "${qsan_android_revision}"
        COMMAND "${Python3_EXECUTABLE}"
            "${qsan_android_root}/tools/android/create-asset-revision.py"
            "${qsan_android_revision_inputs}" "${qsan_android_revision}"
        DEPENDS "${qsan_android_root}/tools/android/create-asset-revision.py"
            "${qsan_android_revision_inputs}" ${qsan_android_files}
            "${qsan_android_descriptor}" "${qsan_android_inventory}"
        VERBATIM
    )
    set_source_files_properties("${qsan_android_revision}" PROPERTIES
        GENERATED TRUE QT_RESOURCE_ALIAS "android-content-revision.txt")
    qt_add_resources(${target} qsan_android_runtime
        PREFIX "/assets"
        BASE "${qsan_android_root}"
        FILES ${qsan_android_files}
    )
    qt_add_resources(${target} qsan_android_revision
        PREFIX "/"
        FILES "${qsan_android_revision}"
    )
endfunction()
