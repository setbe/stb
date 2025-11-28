cmake_minimum_required(VERSION 3.22)

project(freestanding_stb LANGUAGES CXX)

# --- User options ----------------------------------------------------------
option(LEGACY_CPP14 "Use legacy C++14 compiler (default ON)" ON)
option(BUILD_SHARED_LIBS "Build shared libs" OFF)

# Add custom configurations for multi-config generators (Visual Studio / Xcode)
if(MSVC)
    # ensure the custom configurations exist for multi-config generators
    set(default_configs "Debug;Release;ReleaseMini;ReleaseNoConsole;ReleaseMiniNoConsole")
    if(NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "${default_configs}" CACHE STRING "Available build configurations" FORCE)
    else()
        list(APPEND CMAKE_CONFIGURATION_TYPES "ReleaseMini" "ReleaseNoConsole" "ReleaseMiniNoConsole")
        list(REMOVE_DUPLICATES CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING "Available build configurations" FORCE)
    endif()

    # 32-bit platform for VS
    if(NOT DEFINED CMAKE_GENERATOR_PLATFORM)
        set(CMAKE_GENERATOR_PLATFORM "Win32" CACHE STRING "Platform for Visual Studio" FORCE)
    endif()
endif()

# --- Language standard -----------------------------------------------------
if(LEGACY_CPP14)
    set(CMAKE_CXX_STANDARD 14)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Global flags mapping -------------------------------------------------

include_directories("src/include/")

# We'll apply flags per-config and per-compiler using generator expressions.
# MSVC: map exact switches requested by user. Paths and PDB names are generic here.

# MSVC flags for ReleaseMini (32-bit oriented in your provided list)
set(msvc_release_mini_flags
    "/permissive-"
    "/GS-"
    "/TP"
    "/W3"
    "/Gy"
    "/Zc:wchar_t"
    "/Gm-"
    "/O1"
    "/Ob1"
    "/sdl-"
    "/Zc:inline"
    "/fp:fast"
    "/D;NDEBUG"
    "/utf-8"
    "/fp:except-"
    "/errorReport:none"
    "/WX-"
    "/Zc:forScope"
    "/GR-"
    "/Gz"
    "/Oy"
    "/MT"
    "/FC"
    "/nologo"
    "/Os"
    "/diagnostics:column"
    # Additional options
    "/Zc:threadSafeInit-"
    "/Zl"
    "/Oi-" # essential for freestanding
)

# MSVC flags for Release
set(msvc_release_flags
    "/permissive-"
    "/GS-"
    "/TP"
    "/W3"
    "/Gy"
    "/Zc:wchar_t"
    "/Gm-"
    "/O2"
    "/Ob2"
    "/sdl-"
    "/Zc:inline"
    "/fp:precise"
    "/D;NDEBUG"
    "/utf-8"
    "/errorReport:none"
    "/WX-"
    "/Zc:forScope"
    "/GR-"
    "/Gr"
    "/Oy"
    "/MT"
    "/FC"
    "/nologo"
    "/Fo%OUTDIR%"
    "/Ot"
    "/diagnostics:column"
    # Additional options
    "/Zc:threadSafeInit-"
    "/Zl"
    "/Oi-"
)

# MSVC flags for Debug (keep defaults plus debug info)
set(msvc_debug_flags
    "/Zi"
    "/W3"
    "/GR"
    "/Od"
    "/MDd"
)

# GCC/Clang equivalents (best-effort mapping)
set(unix_release_march_flags
    "-O3"
    "-march=native"
    "-ffunction-sections"
    "-fdata-sections"
    "-fno-exceptions"
    "-fno-rtti"
    "-ffast-math"
    "-fno-math-errno"
    "-fno-signaling-nans"
    "-fomit-frame-pointer"
    "-pipe"
    "-flto"
)
set(unix_release_mini_flags
    "-O1"
    "-Os"
    "-ffunction-sections"
    "-fdata-sections"
    "-fno-exceptions"
    "-fno-rtti"
    "-fomit-frame-pointer"
    "-fno-math-errno"
    "-ffast-math"
    "-fno-signaling-nans"
    "-pipe"
)
set(unix_debug_flags
    "-O0"
    "-g"
    "-fno-omit-frame-pointer"
)

# Helper: add common compile options to a target using generator expressions
function(configure_target_for_all t)
    # Check if the target is an INTERFACE library
    get_target_property(target_type ${t} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()

    # MSVC: specific per-config flags
    target_compile_options(${t} PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:ReleaseMini>>:${msvc_release_mini_flags}>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:${msvc_release_flags}>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:${msvc_debug_flags}>
        # ReleaseNoConsole will reuse Release flags but we may add WIN32 subsystem at link-time
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:ReleaseMiniNoConsole>>:${msvc_release_mini_flags}>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:ReleaseNoConsole>>:${msvc_release_flags}>

        # GCC/Clang mapping
        $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Release>>:${unix_release_march_flags}>
        $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:ReleaseMini>>:${unix_release_mini_flags}>
        $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Debug>>:${unix_debug_flags}>
    )

    # Linker flags for GCC/Clang to drop unused sections
    if(NOT MSVC)
        target_link_options(${t} PRIVATE
            $<$<CONFIG:Release>:LINKER:--gc-sections>
            $<$<CONFIG:ReleaseMini>:LINKER:--gc-sections>
        )
    endif()
endfunction()

# Fix: CMake needs per-config variables for custom configurations
foreach(cfg IN ITEMS ReleaseMini ReleaseNoConsole ReleaseMiniNoConsole)
    string(TOUPPER "${cfg}" CFG_UP)

    if(NOT DEFINED CMAKE_C_FLAGS_${CFG_UP})
        set(CMAKE_C_FLAGS_${CFG_UP} "")
    endif()

    if(NOT DEFINED CMAKE_CXX_FLAGS_${CFG_UP})
        set(CMAKE_CXX_FLAGS_${CFG_UP} "")
    endif()

    if(NOT DEFINED CMAKE_EXE_LINKER_FLAGS_${CFG_UP})
        set(CMAKE_EXE_LINKER_FLAGS_${CFG_UP} "")
    endif()

    if(NOT DEFINED CMAKE_SHARED_LINKER_FLAGS_${CFG_UP})
        set(CMAKE_SHARED_LINKER_FLAGS_${CFG_UP} "")
    endif()

    if(NOT DEFINED CMAKE_STATIC_LINKER_FLAGS_${CFG_UP})
        set(CMAKE_STATIC_LINKER_FLAGS_${CFG_UP} "")
    endif()
endforeach()



# Common definitions
add_compile_definitions($<$<CONFIG:Release>:NDEBUG>)

# --- Sources ---------------------------------------------------------------
set(SOURCES_TRUETYPE        "src/include/stb_truetype.hpp")
set(SOURCES_IMAGE           "src/include/stb_image.hpp")
set(SOURCES_IMAGE_WRITE     "src/include/stb_image_write.hpp")
set(SOURCES_IMAGE_RESIZE2   "src/include/stb_image_resize2.hpp")

# --- Header-only stb libraries (INTERFACE) --------------------------------
function(add_freestanding_single_header_lib name header)
    add_library(${name} INTERFACE)
    target_sources(${name} INTERFACE ${header})
    target_include_directories(${name} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/include>
    )
endfunction()

add_freestanding_single_header_lib(truetype      ${SOURCES_TRUETYPE})
add_freestanding_single_header_lib(image         ${SOURCES_IMAGE})
add_freestanding_single_header_lib(image_write   ${SOURCES_IMAGE_WRITE})
add_freestanding_single_header_lib(image_resize2 ${SOURCES_IMAGE_RESIZE2})

# --- ReleaseNoConsole: set subsystem on Windows to use WinMain entrypoint ------------
# We cannot know which targets will be executables here; provide a helper function.
function(setup_release_no_console_for_exe exe_target)
    if(MSVC)
        # Add link option only for the ReleaseNoConsole configuration:
        # No console (Subsystem:Windows) + WinMain as entry point
        target_link_options(${exe_target} PRIVATE
            $<$<CONFIG:ReleaseNoConsole>:/SUBSYSTEM:WINDOWS>
            $<$<CONFIG:ReleaseNoConsole>:/ENTRY:WinMain>

            $<$<CONFIG:ReleaseMiniNoConsole>:/SUBSYSTEM:WINDOWS>
            $<$<CONFIG:ReleaseMiniNoConsole>:/ENTRY:WinMain>
        )
    else()
        # For Unix/Clang/GCC there is no "no-console" subsystem; executables can be linked normally.
    endif()
endfunction()

# --- Test libraries (STATIC with .cpp, visible in Visual Studio) ----------
function(add_freestanding_test name headers test_cpp)
    add_executable(${name} ${test_cpp})
    setup_release_no_console_for_exe(${name})

    # Define CONSOLE for all configurations,
    # except for ReleaseNoConsole and ReleaseMiniNoConsole
    target_compile_definitions(${name} PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:Release>,$<CONFIG:ReleaseMini>>:CONSOLE>)

    # Make stb headers visible in IDE by attaching them to the target:
    target_sources(${name} PRIVATE ${headers})

    target_include_directories(${name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/include>
    )

    configure_target_for_all(${name})
endfunction()

add_freestanding_test(test_tt
    "${SOURCES_TRUETYPE}"
    "src/test/stb_truetype.cpp"
)

add_freestanding_test(test_img
    "${SOURCES_IMAGE}"
    "src/test/stb_image.cpp"
)

add_freestanding_test(test_img_write
    "${SOURCES_IMAGE_WRITE}"
    "src/test/stb_image_write.cpp"
)

add_freestanding_test(test_img_resize2
    "${SOURCES_IMAGE_RESIZE2}"
    "src/test/stb_image_resize2.cpp"
)

# --- Usage notes -------------------------------------------------------------
# - Fill the SOURCES_* variables or replace the glob patterns with explicit files.
# - For single-config generators (Makefile/Ninja) set -DCMAKE_BUILD_TYPE=ReleaseMini to build that flavor.
# - LEGACY_CPP14=ON sets C++14. Turn OFF to use C++20.
# - For MSVC static runtime (/MT) is enforced in the per-config flags above.
# - To mark an executable as using WinAPI entrypoint, call setup_release_no_console_for_exe(<your_exe>)

message(STATUS "Configured freestanding CMake project (C++ standard=${CMAKE_CXX_STANDARD})")
