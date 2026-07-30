if(NOT DEFINED QSAN_SWIG_DIR OR NOT DEFINED QSAN_SWIG_WRAPPER)
    message(FATAL_ERROR "QSAN_SWIG_DIR and QSAN_SWIG_WRAPPER are required")
endif()

if(NOT EXISTS "${QSAN_SWIG_WRAPPER}")
    message(FATAL_ERROR "Missing generated SWIG wrapper: ${QSAN_SWIG_WRAPPER}")
endif()

file(GLOB swig_interfaces "${QSAN_SWIG_DIR}/*.i")
foreach(interface IN LISTS swig_interfaces)
    if("${interface}" IS_NEWER_THAN "${QSAN_SWIG_WRAPPER}")
        message(FATAL_ERROR
            "SWIG wrapper is stale: ${interface} is newer than ${QSAN_SWIG_WRAPPER}. "
            "Build target swig-regenerate, review the generated file, then rebuild.")
    endif()
endforeach()
