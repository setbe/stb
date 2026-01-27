cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Custom configs for Visual Studio (must be BEFORE project())
# NOTE: Don't use if(MSVC) here, MSVC is not known yet.
# ---------------------------------------------------------------------------
set(STB_CUSTOM_CONFIGS "ReleaseMini;ReleaseNoConsole;ReleaseMiniNoConsole")

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_CONFIGURATION_TYPES
        "Debug;Release;${STB_CUSTOM_CONFIGS}"
        CACHE STRING "Available build configurations" FORCE
    )
endif()

project(freestanding_stb LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Seed missing per-config flag variables (must be AFTER project())
# to avoid: CMAKE_EXE_LINKER_FLAGS_<CFG> not set
# ---------------------------------------------------------------------------
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    foreach(cfg IN ITEMS ReleaseMini ReleaseNoConsole ReleaseMiniNoConsole)
        string(TOUPPER "${cfg}" CFGU)

        foreach(var IN ITEMS
            CMAKE_C_FLAGS
            CMAKE_CXX_FLAGS
            CMAKE_EXE_LINKER_FLAGS
            CMAKE_SHARED_LINKER_FLAGS
            CMAKE_MODULE_LINKER_FLAGS
        )
            # If not defined, copy from Release as baseline.
            if(NOT DEFINED ${var}_${CFGU})
                set(${var}_${CFGU} "${${var}_RELEASE}" CACHE STRING "" FORCE)
            endif()
        endforeach()
    endforeach()
endif()


# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(LEGACY_CPP14 "Use legacy C++14 compiler (default ON)" ON)
option(BUILD_SHARED_LIBS "Build shared libs" OFF)

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
    "/Oi-"     # disable intrinsics (essential for freestanding)
    "/EHs-"  # disable C++ exceptions (no unwind tables)
    "/EHc-"    # no extern "C" unwind
    "/Gm-"
    "/Gy"
    "/Zc:inline"
    "/Zc:wchar_t"
    "/Zc:forScope"
    "/sdl-"
    "/errorReport:none"
    "/WX-"
    "/D;NDEBUG"
    "/D_HAS_EXCEPTIONS=0"
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
function(stb_apply_common target)
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

function(stb_apply_freestanding target)
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

function(stb_apply_debug target)
    target_compile_options(${target} PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:${msvc_debug_compile}>
        $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Debug>>:${unix_debug_compile}>
    )
endfunction()

function(stb_no_console target)
    if(MSVC)
        target_link_options(${target} PRIVATE
            $<$<CONFIG:ReleaseNoConsole>:/SUBSYSTEM:WINDOWS>
            $<$<CONFIG:ReleaseNoConsole>:/ENTRY:WinMain>
            $<$<CONFIG:ReleaseMiniNoConsole>:/SUBSYSTEM:WINDOWS>
            $<$<CONFIG:ReleaseMiniNoConsole>:/ENTRY:WinMain>
        )
    endif()
endfunction()

function(stb_add_header_only name)
    add_library(${name} INTERFACE)
    target_include_directories(${name} INTERFACE
        $<BUILD_INTERFACE:${PROJECT_INCLUDE_DIR}>
    )
    # to show '.hpp' in IDE:
    target_sources(${name} INTERFACE ${ARGN})
endfunction()

function(stb_add_test_exe name)
    # usage: stb_add_test_exe(target cpp_file [headers...])
    set(options)
    set(oneValueArgs CPP)
    set(multiValueArgs HEADERS LIBS)
    cmake_parse_arguments(T "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${name} ${T_CPP} ${T_HEADERS})
    stb_no_console(${name})

    # CONSOLE define only for configs where it set
    target_compile_definitions(${name} PRIVATE
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:Release>,$<CONFIG:ReleaseMini>>:CONSOLE>
    )

    if(T_LIBS)
        target_link_libraries(${name} PRIVATE ${T_LIBS})
    endif()

    stb_apply_common(${name})
    stb_apply_freestanding(${name})
    stb_apply_debug(${name})
endfunction()

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------

# truetype
set(SOURCES_TRUETYPE
    "stb_truetype/stb_truetype.hpp"
    "stb_truetype/detail/enums.hpp"
    "stb_truetype/detail/cff_parser.hpp"
    "stb_truetype/detail/edges.hpp"
    "stb_truetype/detail/raster_scratch.hpp"
    "stb_truetype/detail/math_integration.hpp"
    "stb_truetype/detail/libc_integration.hpp"
)
set(SOURCES_TRUETYPE_CATCH
    ${SOURCES_TRUETYPE}
    "stb_truetype/stb_truetype.h"   # original for byte-diff tests
)

# truetype stream
set(SOURCES_TRUETYPE_STREAM
    "stb_truetype_stream/stb_truetype_stream.hpp"

    "stb_truetype_stream/codepoints/stbtt_codepoints.hpp"
    "stb_truetype_stream/codepoints/stbtt_codepoints_stream.hpp"

    "stb_truetype_stream/codepoints/script/arabic.hpp"
    "stb_truetype_stream/codepoints/script/cjk.hpp"
    "stb_truetype_stream/codepoints/script/cyrillic.hpp"
    "stb_truetype_stream/codepoints/script/devanagari.hpp"
    "stb_truetype_stream/codepoints/script/greek.hpp"
    "stb_truetype_stream/codepoints/script/hebrew.hpp"
    "stb_truetype_stream/codepoints/script/jouyou_kanji.hpp"
    "stb_truetype_stream/codepoints/script/kana.hpp"
    "stb_truetype_stream/codepoints/script/latin.hpp"
)

# image
set(SOURCES_IMAGE         "stb_image/stb_image.hpp")

# image write
set(SOURCES_IMAGE_WRITE
    "stb_image_write/stb_image_write.hpp"
    "stb_image_write/detail/libc_integration.hpp"
    "stb_image_write/detail/zlib.hpp")
set(SOURCES_IMAGE_WRITE_CATCH
    ${SOURCES_IMAGE_WRITE}
    "stb_image_write/stb_image_write.h"   # original for byte-diff tests
)

# image resize
set(SOURCES_IMAGE_RESIZE2 "stb_image_resize2/stb_image_resize2.hpp")

# -----------------------------------------------------------------------------
# Header-only interface targets
# -----------------------------------------------------------------------------
stb_add_header_only(truetype        ${SOURCES_TRUETYPE})
stb_add_header_only(truetype_stream ${SOURCES_TRUETYPE_STREAM})
stb_add_header_only(image           ${SOURCES_IMAGE})
stb_add_header_only(image_write     ${SOURCES_IMAGE_WRITE})
stb_add_header_only(image_resize2   ${SOURCES_IMAGE_RESIZE2})

# -----------------------------------------------------------------------------
# Freestanding examples
# -----------------------------------------------------------------------------

# truetype
stb_add_test_exe(tt_example
    CPP "test/stbtt_example.cpp"
    HEADERS ${SOURCES_TRUETYPE}
    LIBS truetype
)

# truetype stream (one character)
stb_add_test_exe(tt_stream_example
    CPP "test/stbtt_stream_example.cpp"
    HEADERS ${SOURCES_TRUETYPE_STREAM}
    LIBS truetype_stream
)

# truetype stream (whole bitmap)
stb_add_test_exe(tt_stream_bitmap_example
    CPP "test/stbtt_stream_bitmap_example.cpp"
    HEADERS ${SOURCES_TRUETYPE_STREAM}
    LIBS truetype_stream
         image_write
)


# image
stb_add_test_exe(i_example
    CPP "test/stbi_example.cpp"
    HEADERS ${SOURCES_IMAGE}
    LIBS image
)

# image write
stb_add_test_exe(iw_example
    CPP "test/stbiw_example.cpp"
    HEADERS ${SOURCES_IMAGE_WRITE}
    LIBS image_write
)

# image resize
stb_add_test_exe(ir_example
    CPP "test/stbir_example.cpp"
    HEADERS ${SOURCES_IMAGE_RESIZE2}
    LIBS image_resize2
)

# -----------------------------------------------------------------------------
# General tests
# -----------------------------------------------------------------------------

# truetype
stb_add_test_exe(tt_catch
    CPP "test/stbtt_catch.cpp"
    HEADERS ${SOURCES_TRUETYPE_CATCH}
    LIBS truetype
)
stb_add_test_exe(tt_bench
    CPP "test/stbtt_bench.cpp"
)

# image write
stb_add_test_exe(iw_catch
    CPP "test/stbiw_catch.cpp"
    HEADERS ${SOURCES_IMAGE_WRITE_CATCH}
    LIBS image_write
)
stb_add_test_exe(iw_bench
    CPP "test/stbiw_bench.cpp"
)

message(STATUS "Configured ${PROJECT_NAME} (C++${CMAKE_CXX_STANDARD})")
message(STATUS "Freestanding configs: ${FREESTANDING_CONFIGS}")
