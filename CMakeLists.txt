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
option(GRACE_ENABLE_ANGELSCRIPT "Build the AngelScript script backend"      OFF)

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
# grace_script
#
# The platform-neutral script-host contract: engine/module/function/context
# handles and the registration surface. The headers carry no scripting-language
# types; concrete backends live in their own modules behind the contract
# (decision 32).
# ---------------------------------------------------------------------------
add_library(grace_script STATIC
    src/script/script_types.cpp)
target_include_directories(grace_script PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_script PUBLIC grace_core PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace_null_script
#
# Headless script backend: the contract's protocol reference. It records the
# registration surface and the execution requests and counts protocol
# violations; it cannot compile or run scripts (it has no language).
# ---------------------------------------------------------------------------
add_library(grace_null_script STATIC
    src/script/null_script_backend.cpp)
target_link_libraries(grace_null_script PUBLIC grace_script PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace_angelscript_script
#
# The script contract's real implementation (decision 33): the AngelScript
# adapter, the execution reference. It maps the contract onto the SDK's
# engine/module/function/context and registration surface.
# ---------------------------------------------------------------------------
if(GRACE_ENABLE_ANGELSCRIPT)
    add_library(grace_angelscript_script STATIC
        src/script/angelscript_backend.cpp)
    target_include_directories(grace_angelscript_script SYSTEM PRIVATE
        ${GRACE_ANGELSCRIPT_DIR}/include)
    target_link_libraries(grace_angelscript_script PUBLIC grace_script
        PRIVATE grace_warnings
        ${GRACE_ANGELSCRIPT_BUILD_DIR}/libangelscript.a)
endif()

# ---------------------------------------------------------------------------
# grace_visual
#
# The platform-neutral visual-scripting contract: registry, block and pin
# descriptions, graph descriptions and the compile operation. The headers
# carry no scripting-language types; the visual layer registers its blocks
# through the script contract (architecture §4.2).
# ---------------------------------------------------------------------------
add_library(grace_visual STATIC
    src/visual/visual_types.cpp)
target_include_directories(grace_visual PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_visual PUBLIC grace_script PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace_null_visual
#
# Headless visual-scripting backend: the contract's protocol reference. It
# records the registration surface and the compile requests and counts
# protocol violations; it cannot compile graphs (it has no script host
# behind it).
# ---------------------------------------------------------------------------
add_library(grace_null_visual STATIC
    src/visual/null_visual_backend.cpp)
target_link_libraries(grace_null_visual PUBLIC grace_visual PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace_angelscript_visual
#
# The visual contract's real implementation (decision 34): the graph
# compiler that turns a graph into a callable entry point through the
# script contract's registration surface (architecture §4.2). It maps the
# contract onto the script backend the application owns.
# ---------------------------------------------------------------------------
if(GRACE_ENABLE_ANGELSCRIPT)
    add_library(grace_angelscript_visual STATIC
        src/visual/angelscript_visual_backend.cpp)
    target_link_libraries(grace_angelscript_visual PUBLIC grace_visual
        grace_script PRIVATE grace_warnings)
endif()

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

set(GRACE_ANGELSCRIPT_DIR "" CACHE PATH
    "AngelScript source root (sdk/angelscript/include/)")
set(GRACE_ANGELSCRIPT_BUILD_DIR "" CACHE PATH
    "AngelScript build output (libangelscript.a)")

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
# grace_owd
#
# The OWD container (docs/resource-container.md): the writer, the reader and
# the overlay loader, on zlib's Deflate. The codec is internal; the contract
# is the EntryDesc and the Payload.
# ---------------------------------------------------------------------------

find_package(ZLIB REQUIRED)

add_library(grace_owd STATIC
    src/owd/internal.cpp
    src/owd/owd.cpp)
target_include_directories(grace_owd PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_owd PUBLIC ZLIB::ZLIB PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# grace_scene
#
# The scene module (docs/grace-architecture.md section 3.8): data-driven
# entities, selectable spatial partitioning (flat, grid, octree, quadtree),
# scene queries and level of detail. The serialisation strategy (decision
# 36) writes the scene's state into the OWD container as one entry; the
# scene stays plain data, so the container's own checksums and the overlay
# loader carry the integrity.
# ---------------------------------------------------------------------------
add_library(grace_scene STATIC
    src/scene/scene_types.cpp
    src/scene/scene.cpp
    src/scene/scene_serial.cpp)
target_include_directories(grace_scene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_scene PUBLIC grace_core PRIVATE grace_warnings)

# ---------------------------------------------------------------------------
# owdtool
#
# The container's command line: pack a directory, unpack a container, list
# one. A build and test utility; not a library dependency of the engine.
# ---------------------------------------------------------------------------

add_executable(owdtool
    src/owd/owdtool.cpp)

target_link_libraries(owdtool PRIVATE grace_owd grace_warnings)

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
