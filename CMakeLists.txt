cmake_minimum_required(VERSION 3.16)

project(OpenGrace
    VERSION 0.1.0
    DESCRIPTION "Independent reimplementation of the GRACE 2 engine architecture"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(GRACE_BUILD_TESTS        "Build and register the deterministic tests" ON)
option(GRACE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"         ON)
option(GRACE_ENABLE_OPENGL      "Build the Linux OpenGL backend"            OFF)
option(GRACE_ENABLE_PHYSX       "Build the PhysX 3.4 physics backend"       OFF)

# ---------------------------------------------------------------------------
# Warning set
#
# The engine targets several compilers. The strict set below is applied through
# an interface target so that every module inherits it without repeating flags,
# and so that a third-party dependency can opt out by not linking it.
# ---------------------------------------------------------------------------

add_library(grace_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(grace_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wold-style-cast
        -Wnon-virtual-dtor
        -Woverloaded-virtual)

    if(GRACE_WARNINGS_AS_ERRORS)
        target_compile_options(grace_warnings INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# grace_render
#
# The platform-neutral rendering contract. Header-only: the interface carries
# no platform, windowing or graphics-API types, and concrete backends live in
# their own modules behind it.
# ---------------------------------------------------------------------------

add_library(grace_render INTERFACE)

target_include_directories(grace_render INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# ---------------------------------------------------------------------------
# grace_null_render
#
# Headless render backend: records the call sequence and counts protocol
# violations. No windowing or graphics dependency.
# ---------------------------------------------------------------------------

add_library(grace_null_render STATIC
    src/render/null_render_backend.cpp)

target_link_libraries(grace_null_render PUBLIC grace_render PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace_core
#
# The core module owns the object model, resource manager, threading, math,
# curves and the profiler. It must stay free of any graphics, audio or
# platform dependency so that every other module can build on it. The
# application lifecycle drives a render backend through the platform-neutral
# contract, so the contract header is part of grace_core's public interface.
# ---------------------------------------------------------------------------

add_library(grace_core STATIC
    src/core/version.cpp
    src/core/fixed_step_clock.cpp
    src/core/application.cpp)

target_include_directories(grace_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(grace_core PUBLIC grace_render PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace_app
#
# Command-line driver for the application lifecycle. Headless (null backend)
# in this milestone; the OpenGL backend is a separate module.
# ---------------------------------------------------------------------------

add_executable(grace_app
    src/app/main.cpp)

target_link_libraries(grace_app PRIVATE grace_core grace_null_render grace_warnings)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(GRACE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
