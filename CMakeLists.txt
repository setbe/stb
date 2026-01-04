cmake_minimum_required(VERSION 3.22)

project(freestanding_stb LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(LEGACY_CPP14 "Use legacy C++14 compiler (default ON)" ON)
option(BUILD_SHARED_LIBS "Build shared libs" OFF)

# -----------------------------------------------------------------------------
# Build configurations (MSVC multi-config generators)
# -----------------------------------------------------------------------------
if(MSVC)
    set(_default_configs "Debug;Release;ReleaseMini;ReleaseNoConsole;ReleaseMiniNoConsole")

    if(CMAKE_CONFIGURATION_TYPES)
        # Multi-config generator (Visual Studio, Xcode)
        list(APPEND CMAKE_CONFIGURATION_TYPES "ReleaseMini" "ReleaseNoConsole" "ReleaseMiniNoConsole")
        list(REMOVE_DUPLICATES CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING "Available build configurations" FORCE)
    else()
        # Single-config generator (Ninja, Makefiles)
        # (CMAKE_BUILD_TYPE is selected manually: -DCMAKE_BUILD_TYPE=ReleaseMini)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${_default_configs})
    endif()

    # Default Win32 for VS
    if(NOT DEFINED CMAKE_GENERATOR_PLATFORM)
        set(CMAKE_GENERATOR_PLATFORM "Win32" CACHE STRING "Platform for Visual Studio" FORCE)
    endif()
endif()

# -----------------------------------------------------------------------------
# Language standard
# -----------------------------------------------------------------------------
if(LEGACY_CPP14)
    set(CMAKE_CXX_STANDARD 14)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Config groups
# -----------------------------------------------------------------------------
set(FREESTANDING_CONFIGS ReleaseMini ReleaseNoConsole ReleaseMiniNoConsole)

# Helper generator expression: true when freestanding config
set(_GE_FREESTANDING
    "$<OR:$<CONFIG:ReleaseMini>,$<CONFIG:ReleaseNoConsole>,$<CONFIG:ReleaseMiniNoConsole>>"
)

# -----------------------------------------------------------------------------
# Flags (cleanly separated: common vs freestanding-only)
# -----------------------------------------------------------------------------
set(msvc_common_compile
    "/permissive-"
    "/utf-8"
    "/diagnostics:column"
    "/nologo"
)

# Freestanding base (applies to all Release* freestanding configs)
set(msvc_freestanding_base
    "/TP"
    "/W3"
    "/FC"
    "/GS-"
    "/GR-"
    "/Zl"
    "/Zc:threadSafeInit-"
    "/Oi-"     # essential for freestanding (disable intrinsics)
    "/Gm-"
    "/Gy"
    "/Zc:inline"
    "/Zc:wchar_t"
    "/Zc:forScope"
    "/sdl-"
    "/errorReport:none"
    "/WX-"
    "/D;NDEBUG"
)

# Freestanding "full" (ReleaseNoConsole)
set(msvc_freestanding_full_opt
    "/O2"
    "/Ob2"
    "/fp:precise"
    "/Ot"
    "/Oy"
)

# Freestanding "mini" (ReleaseMini, ReleaseMiniNoConsole)
set(msvc_freestanding_mini_opt
    "/O1"
    "/Ob1"
    "/fp:fast"
    "/Os"
    "/Oy"
)

# Debug
set(msvc_debug_compile
    "/Zi"
    "/W3"
    "/GR"
    "/Od"
)

# GCC/Clang
set(unix_common_compile
    "-pipe"
)

set(unix_freestanding_base
    "-ffunction-sections"
    "-fdata-sections"
    "-fno-exceptions"
    "-fno-rtti"
    "-fno-math-errno"
    "-fno-signaling-nans"
    "-fomit-frame-pointer"
)

set(unix_freestanding_full_opt
    "-O3"
    "-flto"
    "-ffast-math"
)

set(unix_freestanding_mini_opt
    "-O1"
    "-Os"
    "-ffast-math"
)

set(unix_debug_compile
    "-O0"
    "-g"
    "-fno-omit-frame-pointer"
)

# -----------------------------------------------------------------------------
# Common include root
# -----------------------------------------------------------------------------
set(PROJECT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/src/include")

# -----------------------------------------------------------------------------
# Functions
# -----------------------------------------------------------------------------
function(io_apply_common target)
    target_include_directories(${target} PRIVATE
        $<BUILD_INTERFACE:${PROJECT_INCLUDE_DIR}>
    )

    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:Release>:NDEBUG>
    )

    # compiler-id common flags
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:${msvc_common_compile}>
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:${unix_common_compile}>
    )
endfunction()

function(io_apply_freestanding target)
    # MSVC: apply ONLY for freestanding configs
    target_compile_options(${target} PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,${_GE_FREESTANDING}>:${msvc_freestanding_base}>

        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:ReleaseNoConsole>>:${msvc_freestanding_full_opt}>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:ReleaseMini>>:${msvc_freestanding_mini_opt}>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:ReleaseMiniNoConsole>>:${msvc_freestanding_mini_opt}>
    )

    # MSVC runtime: freestanding => /MT; otherwise leave default (MD/MDd from CMake)
    if(MSVC)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY
            "$<IF:${_GE_FREESTANDING},MultiThreaded,$<IF:$<CONFIG:Debug>,MultiThreadedDebugDLL,MultiThreadedDLL>>"
        )
    endif()

    # GCC/Clang freestanding compile options
    target_compile_options(${target} PRIVATE
        $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,${_GE_FREESTANDING}>:${unix_freestanding_base}>

        $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:ReleaseNoConsole>>:${unix_freestanding_full_opt}>
        $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:ReleaseMini>>:${unix_freestanding_mini_opt}>
        $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:ReleaseMiniNoConsole>>:${unix_freestanding_mini_opt}>
    )

    # GC sections on Unix for freestanding
    if(NOT MSVC)
        target_link_options(${target} PRIVATE
            $<$<AND:$<OR:$<CONFIG:ReleaseMini>,$<CONFIG:ReleaseNoConsole>,$<CONFIG:ReleaseMiniNoConsole>>:LINKER:--gc-sections>
        )
    endif()
endfunction()

function(io_apply_debug target)
    target_compile_options(${target} PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:${msvc_debug_compile}>
        $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Debug>>:${unix_debug_compile}>
    )
endfunction()

function(io_no_console target)
    if(MSVC)
        target_link_options(${target} PRIVATE
            $<$<CONFIG:ReleaseNoConsole>:/SUBSYSTEM:WINDOWS>
            $<$<CONFIG:ReleaseNoConsole>:/ENTRY:WinMain>
            $<$<CONFIG:ReleaseMiniNoConsole>:/SUBSYSTEM:WINDOWS>
            $<$<CONFIG:ReleaseMiniNoConsole>:/ENTRY:WinMain>
        )
    endif()
endfunction()

function(io_add_header_only name)
    add_library(${name} INTERFACE)
    target_include_directories(${name} INTERFACE
        $<BUILD_INTERFACE:${PROJECT_INCLUDE_DIR}>
    )
    # to show '.hpp' in IDE:
    target_sources(${name} INTERFACE ${ARGN})
endfunction()

function(io_add_test_exe name)
    # usage: io_add_test_exe(target cpp_file [headers...])
    set(options)
    set(oneValueArgs CPP)
    set(multiValueArgs HEADERS LIBS)
    cmake_parse_arguments(T "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${name} ${T_CPP} ${T_HEADERS})
    io_no_console(${name})

    # CONSOLE define only for configs where it set
    target_compile_definitions(${name} PRIVATE
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:Release>,$<CONFIG:ReleaseMini>>:CONSOLE>
    )

    if(T_LIBS)
        target_link_libraries(${name} PRIVATE ${T_LIBS})
    endif()

    io_apply_common(${name})
    io_apply_freestanding(${name})
    io_apply_debug(${name})
endfunction()

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
set(SOURCES_TRUETYPE
    "src/include/stbtt/stb_truetype.hpp"
    "src/include/stbtt/detail/cff_parser.hpp"
    "src/include/stbtt/detail/edges.hpp"
    "src/include/stbtt/detail/enums.hpp"
    "src/include/stbtt/detail/math_integration.hpp"
    "src/include/stbtt/detail/libc_integration.hpp"
    "src/include/stbtt/detail/raster_scratch.hpp"
)

set(SOURCES_TRUETYPE_CATCH
    ${SOURCES_TRUETYPE}
    "src/include/stbtt/stb_truetype.h"   # original for byte-diff tests
)

set(SOURCES_IMAGE         "src/include/stb_image.hpp")
set(SOURCES_IMAGE_WRITE   "src/include/stb_image_write.hpp")
set(SOURCES_IMAGE_RESIZE2 "src/include/stb_image_resize2.hpp")

# -----------------------------------------------------------------------------
# Header-only interface targets
# -----------------------------------------------------------------------------
io_add_header_only(truetype      ${SOURCES_TRUETYPE})
io_add_header_only(image         ${SOURCES_IMAGE})
io_add_header_only(image_write   ${SOURCES_IMAGE_WRITE})
io_add_header_only(image_resize2 ${SOURCES_IMAGE_RESIZE2})

# -----------------------------------------------------------------------------
# Freestanding examples
# -----------------------------------------------------------------------------
io_add_test_exe(tt_example
    CPP "src/test/stbtt_example.cpp"
    HEADERS ${SOURCES_TRUETYPE}
    LIBS truetype
)

io_add_test_exe(i_example
    CPP "src/test/stbi_example.cpp"
    HEADERS ${SOURCES_IMAGE}
    LIBS image
)

io_add_test_exe(iw_example
    CPP "src/test/stbiw_example.cpp"
    HEADERS ${SOURCES_IMAGE_WRITE}
    LIBS image_write
)

io_add_test_exe(ir_example
    CPP "src/test/stbir_example.cpp"
    HEADERS ${SOURCES_IMAGE_RESIZE2}
    LIBS image_resize2
)

# -----------------------------------------------------------------------------
# General tests
# -----------------------------------------------------------------------------
io_add_test_exe(tt_catch
    CPP "src/test/stbtt_catch.cpp"
    HEADERS ${SOURCES_TRUETYPE_CATCH}
    LIBS truetype
)

io_add_test_exe(tt_bench
    CPP "src/test/stbtt_bench.cpp"
)

message(STATUS "Configured ${PROJECT_NAME} (C++${CMAKE_CXX_STANDARD})")
message(STATUS "Freestanding configs: ${FREESTANDING_CONFIGS}")
