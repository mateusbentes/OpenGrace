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
option(GRACE_ENABLE_BULLET      "Build the Bullet physics backend"          OFF)
option(GRACE_ENABLE_BOX2D       "Build the Box2D physics backend"           OFF)

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
    src/render/draw_list.cpp
    src/render/render_target.cpp
    src/render/shadow.cpp
    src/render/camera.cpp
    src/render/paraboloid.cpp
    src/render/trapezoid.cpp
    src/render/renderer.cpp)

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
# grace_physics
#
# The platform-neutral physics module: the classical-physics contract plus
# the API-neutral body/joint/field front-end. The headers carry no physics
# API types; concrete backends live in their own modules behind the contract
# (decision 32).
# ---------------------------------------------------------------------------
add_library(grace_physics STATIC
    src/physics/physics_types.cpp)
target_include_directories(grace_physics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_physics PUBLIC grace_core PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace_null_physics
#
# Headless physics backend: deterministic manual integration at the world's
# fixed step. No physics SDK dependency.
# ---------------------------------------------------------------------------
add_library(grace_null_physics STATIC
    src/physics/null_physics_backend.cpp)
target_link_libraries(grace_null_physics PUBLIC grace_physics PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# Optional physics adapters (8c)
#
# Each adapter maps the contract onto one SDK and lives behind its own
# option. The SDK path is set through GRACE_PHYSX_DIR / GRACE_BULLET_DIR /
# GRACE_BOX2D_DIR, the same pattern the OpenGL backend uses for SDL2.
# ---------------------------------------------------------------------------

set(GRACE_PHYSX_DIR "" CACHE PATH "PhysX 3.4 root (Include/, Lib/, PxShared/)")
set(GRACE_BULLET_DIR "" CACHE PATH "Bullet source root (src/ headers)")
set(GRACE_BULLET_BUILD_DIR "" CACHE PATH "Bullet build output (lib*.a)")
set(GRACE_BOX2D_DIR "" CACHE PATH "Box2D source root (include/box2d)")
set(GRACE_BOX2D_BUILD_DIR "" CACHE PATH "Box2D build output (libbox2d.a)")

if(GRACE_ENABLE_PHYSX)
    add_library(grace_physx_physics STATIC
        src/physics/physx_physics_backend.cpp)
    target_include_directories(grace_physx_physics SYSTEM PRIVATE
        ${GRACE_PHYSX_DIR}/Include
        ${GRACE_PHYSX_DIR}/../PxShared/include)
    target_link_libraries(grace_physx_physics PUBLIC grace_physics
        PRIVATE grace_warnings
        ${GRACE_PHYSX_DIR}/Lib/linux64/libPhysX3Extensions.a
        ${GRACE_PHYSX_DIR}/Lib/linux64/libSimulationController.a
        ${GRACE_PHYSX_DIR}/Lib/linux64/libSceneQuery.a
        ${GRACE_PHYSX_DIR}/Lib/linux64/libLowLevelDynamics.a
        ${GRACE_PHYSX_DIR}/Lib/linux64/libLowLevelAABB.a
        ${GRACE_PHYSX_DIR}/Lib/linux64/libLowLevel.a
        ${GRACE_PHYSX_DIR}/Bin/linux64/libPhysX3_x64.so
        ${GRACE_PHYSX_DIR}/Bin/linux64/libPhysX3Common_x64.so
        ${GRACE_PHYSX_DIR}/../PxShared/bin/linux64/libPxFoundation_x64.so
        ${GRACE_PHYSX_DIR}/../PxShared/lib/linux64/libPxTask.a)
endif()

if(GRACE_ENABLE_BULLET)
    add_library(grace_bullet_physics STATIC
        src/physics/bullet_physics_backend.cpp)
    target_include_directories(grace_bullet_physics SYSTEM PRIVATE
        ${GRACE_BULLET_DIR}/src)
    target_link_libraries(grace_bullet_physics PUBLIC grace_physics
        PRIVATE grace_warnings
        ${GRACE_BULLET_BUILD_DIR}/src/BulletDynamics/libBulletDynamics.a
        ${GRACE_BULLET_BUILD_DIR}/src/BulletCollision/libBulletCollision.a
        ${GRACE_BULLET_BUILD_DIR}/src/LinearMath/libLinearMath.a)
endif()

if(GRACE_ENABLE_BOX2D)
    add_library(grace_box2d_physics STATIC
        src/physics/box2d_physics_backend.cpp)
    target_include_directories(grace_box2d_physics SYSTEM PRIVATE
        ${GRACE_BOX2D_DIR}/include)
    target_link_libraries(grace_box2d_physics PUBLIC grace_physics
        PRIVATE grace_warnings
        ${GRACE_BOX2D_BUILD_DIR}/src/libbox2d.a)
endif()

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
        src/render/opengl/opengl_render_target.cpp
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
