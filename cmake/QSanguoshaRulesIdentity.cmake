include_guard(GLOBAL)

# Portable source identities deliberately exclude build paths, timestamps,
# compiler/platform ABI and Debug/Release flags. C++/Lua semantics remain pinned.
function(qsan_rules_source_hash result)
    set(records "qsan-rules-source-v1\n")
    set(paths ${ARGN})
    list(SORT paths)
    foreach(path IN LISTS paths)
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${path}" contents)
        string(REPLACE "\r\n" "\n" contents "${contents}")
        string(SHA256 hash "${contents}")
        string(APPEND records "${path}:${hash}\n")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${paths})
    string(SHA256 hash "${records}")
    set(${result} "${hash}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE qsan_rules_sources CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    src/core/*.cpp src/core/*.h src/package/*.cpp src/package/*.h
    src/scenario/*.cpp src/scenario/*.h src/server/*.cpp src/server/*.h
    src/client/core/*.cpp src/client/core/*.h src/client/runtime/*.cpp src/client/runtime/*.h
    src/lua/*.c src/lua/*.h swig/*.i)
file(GLOB_RECURSE qsan_rules_bindings CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    swig/*.i src/lua/*.h)
file(GLOB_RECURSE qsan_rules_protocol CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    src/core/protocol/*.cpp src/core/protocol/*.h)
list(APPEND qsan_rules_protocol src/core/protocol.h src/core/protocol.cpp)
# These shared client adapters live above src/client/core and are not captured
# by that recursive glob. Their semantics now participate in native ingress.
list(APPEND qsan_rules_sources
    src/client/protocol-interaction-request-builder.cpp src/client/protocol-interaction-request-builder.h
    src/client/interaction-request-factory.cpp src/client/interaction-request-factory.h
    src/client/interaction-command-registry.cpp src/client/interaction-command-registry.h
    src/client/interaction-reply-encoder.cpp src/client/interaction-reply-encoder.h
    cmake/QSanguoshaRulesSession.cmake cmake/QSanguoshaWebClient.cmake)
qsan_rules_source_hash(QSAN_RULES_CPP_HASH ${qsan_rules_sources}
    cmake/QSanguoshaSources.cmake CMakeLists.txt cmake/QSanguoshaRulesIdentity.cmake)
qsan_rules_source_hash(QSAN_RULES_BINDINGS_HASH ${qsan_rules_bindings}
    src/core/protocol/rules-bundle-identity.h)
qsan_rules_source_hash(QSAN_RULES_PROTOCOL_HASH ${qsan_rules_protocol})
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/rules-bundle-build.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rules-bundle-build.h" @ONLY)
target_include_directories(qsanguosha_engine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
