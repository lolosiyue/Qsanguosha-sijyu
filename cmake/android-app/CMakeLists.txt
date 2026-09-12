# Android GUI target factory. The parent directory owns the shared source list
# and adds all platform-neutral resources, sources and link dependencies.

set(qsan_android_gui_sources)
foreach(qsan_android_gui_source IN LISTS QSAN_GUI_SOURCES)
    if(IS_ABSOLUTE "${qsan_android_gui_source}")
        list(APPEND qsan_android_gui_sources "${qsan_android_gui_source}")
    else()
        get_filename_component(qsan_android_gui_absolute
            "${CMAKE_CURRENT_LIST_DIR}/../../${qsan_android_gui_source}" ABSOLUTE)
        list(APPEND qsan_android_gui_sources "${qsan_android_gui_absolute}")
    endif()
endforeach()

# Runtime descriptors and media inventory are added by the parent target.
qt_add_executable(${QSAN_GUI_TARGET} MANUAL_FINALIZATION
    ${qsan_android_gui_sources}
)
