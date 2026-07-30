cmake_minimum_required(VERSION 3.25)

project(CACLauncher VERSION 0.6.10 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "CACLauncher is a Windows-only target.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "CACLauncher must be built with the MSVC x64 toolchain.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "CACLauncher must be configured for x64.")
endif()

add_executable(CACLauncher WIN32
    CACLauncher.cpp
)

target_compile_features(CACLauncher PRIVATE cxx_std_20)
target_compile_definitions(CACLauncher PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_options(CACLauncher PRIVATE
    /W4
    /permissive-
    /EHsc
    /utf-8
)
target_include_directories(CACLauncher PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Keep the launcher independent from an installed VC runtime. The Qt application
# still receives the audited app-local VC143 runtime during staging.
set_property(TARGET CACLauncher PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(CACLauncher PRIVATE
    winhttp
    ws2_32
)
target_link_options(CACLauncher PRIVATE
    "/MANIFEST:EMBED"
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/CACLauncher.manifest"
)

set_target_properties(CACLauncher PROPERTIES
    OUTPUT_NAME "CACLauncher"
)

install(TARGETS CACLauncher RUNTIME DESTINATION .)
