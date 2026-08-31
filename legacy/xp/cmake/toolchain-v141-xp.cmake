# VS2017's v141_xp targets use the Windows 7.1A system libraries but still need
# a Universal CRT SDK.  On newer hosts MSBuild does not always discover that
# UCRT automatically, so make the dependency explicit and overridable.
set(QSAN_XP_WINDOWS_KITS_ROOT
    "C:/Program Files (x86)/Windows Kits/10"
    CACHE PATH "Windows 10 SDK root supplying the v141_xp Universal CRT")
set(QSAN_XP_UCRT_VERSION
    "10.0.17763.0"
    CACHE STRING "Universal CRT SDK version used by the XP legacy build")
set(QSAN_XP_COMPILER
    "C:/Program Files (x86)/Microsoft Visual Studio/2017/BuildTools/VC/Tools/MSVC/14.16.27023/bin/HostX86/x86/cl.exe"
    CACHE FILEPATH "VS2017 x86 compiler used by the v141_xp build")
if(NOT EXISTS "${QSAN_XP_COMPILER}")
    message(FATAL_ERROR "XP compiler not found: ${QSAN_XP_COMPILER}")
endif()
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# v141_xp's compiler-id project omits the host UCRT search path.  The actual
# project targets below carry explicit include/link paths, so skip only that
# unusable probe while retaining the selected Visual Studio generator/toolset.
set(CMAKE_C_COMPILER "${QSAN_XP_COMPILER}")
set(CMAKE_C_COMPILER_ID_RUN TRUE)
set(CMAKE_C_COMPILER_ID MSVC)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER "${QSAN_XP_COMPILER}")
set(CMAKE_CXX_COMPILER_ID_RUN TRUE)
set(CMAKE_CXX_COMPILER_ID MSVC)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
set(CMAKE_C_COMPILER_VERSION "19.16.27051.0")
set(CMAKE_CXX_COMPILER_VERSION "19.16.27051.0")
set(MSVC TRUE CACHE INTERNAL "v141_xp uses the Microsoft compiler")
set(MSVC_VERSION 1916 CACHE INTERNAL "v141_xp compiler version")
set(CMAKE_SIZEOF_VOID_P 4 CACHE INTERNAL "Win32 pointer size")

set(qsan_xp_ucrt_include
    "${QSAN_XP_WINDOWS_KITS_ROOT}/Include/${QSAN_XP_UCRT_VERSION}/ucrt")
set(qsan_xp_ucrt_library
    "${QSAN_XP_WINDOWS_KITS_ROOT}/Lib/${QSAN_XP_UCRT_VERSION}/ucrt/x86")

if(NOT EXISTS "${qsan_xp_ucrt_include}/corecrt.h")
    message(FATAL_ERROR "XP UCRT headers not found: ${qsan_xp_ucrt_include}")
endif()
if(NOT EXISTS "${qsan_xp_ucrt_library}/ucrt.lib")
    message(FATAL_ERROR "XP UCRT library not found: ${qsan_xp_ucrt_library}")
endif()

set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "${QSAN_XP_UCRT_VERSION}" CACHE STRING "" FORCE)
list(APPEND CMAKE_VS_GLOBALS
    "AdditionalIncludeDirectories=${qsan_xp_ucrt_include}"
    "AdditionalLibraryDirectories=${qsan_xp_ucrt_library}")
string(APPEND CMAKE_C_FLAGS_INIT " /I\"${qsan_xp_ucrt_include}\"")
string(APPEND CMAKE_CXX_FLAGS_INIT " /I\"${qsan_xp_ucrt_include}\"")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " /LIBPATH:\"${qsan_xp_ucrt_library}\"")
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " /LIBPATH:\"${qsan_xp_ucrt_library}\"")

unset(qsan_xp_ucrt_include)
unset(qsan_xp_ucrt_library)
