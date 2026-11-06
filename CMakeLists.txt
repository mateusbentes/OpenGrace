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
# grace_core
#
# The core module owns the object model, resource manager, threading, math,
# curves and the profiler. It must stay free of any graphics, audio or
# platform dependency so that every other module can build on it. The math
# headers under include/grace/core/math/ are header-only and dependency-free.
# ---------------------------------------------------------------------------

add_library(grace_core STATIC
    src/core/version.cpp
    src/core/fixed_step_clock.cpp)

target_include_directories(grace_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(grace_core PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace_render
#
# The platform-neutral render module: the backend contract plus the
# API-neutral mesh/shader front-end. The headers carry no platform, windowing
# or graphics-API types; concrete backends live in their own modules behind
# the contract.
# ---------------------------------------------------------------------------

add_library(grace_render STATIC
    src/render/mesh.cpp
    src/render/shader.cpp
    src/render/material.cpp
    src/render/light.cpp
    src/render/draw_list.cpp)

target_include_directories(grace_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(grace_render PUBLIC grace_core PRIVATE grace_warnings)

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
# OpenGL render backend (Linux only)
#
# SDL2 provides window/context/input; OpenGL comes from GLVND (OpenGL::OpenGL,
# never the legacy libGL). Entry points beyond GL 1.1 are resolved at runtime
# through SDL_GL_GetProcAddress by the internal loader in this module.
# ---------------------------------------------------------------------------

if(GRACE_ENABLE_OPENGL)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR
            "GRACE_ENABLE_OPENGL=ON is supported on Linux only "
            "(target platform: native Linux x86_64, OpenGL)")
    endif()

    # SDL2 discovery: config-mode find_package(SDL2) first, pkg-config fallback.
    find_package(SDL2 QUIET)
    if(NOT TARGET SDL2::SDL2)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(SDL2_PC QUIET sdl2)
        endif()
        if(SDL2_PC_FOUND)
            add_library(SDL2::SDL2 INTERFACE IMPORTED)
            target_include_directories(SDL2::SDL2 SYSTEM INTERFACE ${SDL2_PC_INCLUDE_DIRS})
            target_link_libraries(SDL2::SDL2 INTERFACE ${SDL2_PC_LIBRARIES})
            target_link_directories(SDL2::SDL2 INTERFACE ${SDL2_PC_LIBRARY_DIRS})
        endif()
    endif()
    if(NOT TARGET SDL2::SDL2)
        message(FATAL_ERROR
            "GRACE_ENABLE_OPENGL=ON requires SDL2: neither find_package(SDL2) nor "
            "pkg-config sdl2 located it. Install libsdl2-dev or configure with "
            "-DGRACE_ENABLE_OPENGL=OFF.")
    endif()

    set(OpenGL_GL_PREFERENCE GLVND)
    find_package(OpenGL REQUIRED)

    add_library(grace_render_opengl STATIC
        src/render/opengl/gl_loader.cpp
        src/render/opengl/opengl_mesh.cpp
        src/render/opengl/opengl_shader.cpp
        src/render/opengl/opengl_render_backend.cpp)

    target_link_libraries(grace_render_opengl
        PUBLIC grace_render
        PRIVATE grace_warnings SDL2::SDL2 OpenGL::OpenGL)
endif()

# ---------------------------------------------------------------------------
# grace_app
#
# The App module (docs/grace-architecture.md section 2): application lifecycle
# and platform entry points. Depends on grace_core and the render contract,
# never the other way around.
# ---------------------------------------------------------------------------

add_library(grace_app STATIC
    src/app/application.cpp)

target_include_directories(grace_app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(grace_app PUBLIC grace_core grace_render PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace
#
# CLI executable for the App module. Headless (null backend) in this
# milestone; the OpenGL backend is a separate module.
# ---------------------------------------------------------------------------

add_executable(grace
    src/app/main.cpp)

target_link_libraries(grace PRIVATE grace_app grace_null_render grace_warnings)

if(GRACE_ENABLE_OPENGL)
    target_link_libraries(grace PRIVATE grace_render_opengl)
    target_compile_definitions(grace PRIVATE GRACE_HAVE_OPENGL=1)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(GRACE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
