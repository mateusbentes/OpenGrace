cmake_minimum_required(VERSION 3.16)

project(OpenGrace
    VERSION 0.1.0
    DESCRIPTION "Independent reimplementation of the GRACE 2 engine architecture"
    LANGUAGES C CXX)

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
option(GRACE_ENABLE_OPENGL      "Build the OpenGL backend"                 OFF)
option(GRACE_ENABLE_VULKAN      "Build the Vulkan backend"                 OFF)
option(GRACE_ENABLE_METAL       "Build the Metal backend (macOS/iOS)"      OFF)
option(GRACE_ENABLE_PHYSX       "Build the PhysX 3.4 physics backend"       OFF)
option(GRACE_ENABLE_BULLET      "Build the Bullet physics backend"          OFF)
option(GRACE_ENABLE_BOX2D       "Build the Box2D physics backend"          OFF)
option(GRACE_ENABLE_ANGELSCRIPT "Build the AngelScript script backend"     OFF)
option(GRACE_ENABLE_OPENAL      "Build the OpenAL Soft audio backend"      OFF)
option(GRACE_ENABLE_XAUDIO2     "Build the XAudio2 audio backend (Windows)" OFF)
option(GRACE_ENABLE_OPENSL      "Build the OpenSL ES audio backend (Android)" OFF)
option(GRACE_ENABLE_ANDROID_SHELL
    "Build the Android app shell (shared library behind SDLActivity)" OFF)
option(GRACE_ENABLE_FREETYPE    "Build the FreeType GUI font adapter"     OFF)
option(GRACE_ENABLE_THEORA      "Build the Theora video decode adapter"    OFF)

# ---------------------------------------------------------------------------
# Middleware dependencies (pinned git submodules)
#
# The SDKs are checked out at the repository root (see tools/add-submodules.sh)
# and pinned to one commit each in .gitmodules. CMake builds the ones the
# enabled options need into the build tree on first configure and each adapter
# links what it needs. zlib and OpenGL stay system packages (GLVND).
# ---------------------------------------------------------------------------

include(ExternalProject)

# The SDKs are older CMake projects; CMake 4 dropped the <3.5 compatibility,
# so every SDK build passes the policy floor explicitly.
set(GRACE_SDK_POLICY_MINIMUM 3.5)

# The SDKs' install prefix and the shared flags for their CMake builds. Each
# SDK is built in isolation, outside the engine's warning set (the
# grace_warnings interface is not linked to them).
set(GRACE_THIRD_PARTY "${CMAKE_BINARY_DIR}/third-party")
set(GRACE_SDK_CMAKE_ARGS
    -DCMAKE_POLICY_VERSION_MINIMUM=${GRACE_SDK_POLICY_MINIMUM}
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${GRACE_THIRD_PARTY}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
# glibc 2.42+ made bsearch a C23 const-preserving macro, and GCC 15
# flags SDL 2.30's SDL_qsort.c use of it with -Wdiscarded-qualifiers.
# The SDK builds are third-party code outside the engine's warning
# set, so the qualifier warning is relaxed there instead of patching
# the pinned sources. The flag is GCC-only: AppleClang does not know
# it and its "unknown warning option" note poisons the SDK builds'
# flag checks (SDL 2.30's configure fails its -fobjc-arc probe), so
# the relaxation only applies to the GNU compilers.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    list(APPEND GRACE_SDK_CMAKE_ARGS -DCMAKE_C_FLAGS=-Wno-discarded-qualifiers)
endif()
if(CMAKE_TOOLCHAIN_FILE)
    # A cross-compile's toolchain file must reach the SDK builds, or they
    # configure for the build host and the engine's link fails at the
    # platform mismatch.
    list(APPEND GRACE_SDK_CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()
if(CMAKE_CROSSCOMPILING)
    # A cross-compile needs the system name and the binutils at the SDK
    # builds, or they configure for the build host (the SDL2 build would
    # probe linux/version.h on a Windows target).
    list(APPEND GRACE_SDK_CMAKE_ARGS
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
        -DCMAKE_AR=${CMAKE_AR}
        -DCMAKE_RANLIB=${CMAKE_RANLIB})
endif()
if(ANDROID)
    # SDL 2.30's SDL_android.c calls the joystick notification helpers even
    # with the joystick subsystem off, and the NDK's clang makes the implicit
    # declaration an error. The SDK builds are outside the engine's warning
    # set, so the declaration-level error is relaxed there only.
    list(APPEND GRACE_SDK_CMAKE_ARGS
        -DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration)
    # The NDK toolchain file derives its target from ANDROID_ABI and
    # ANDROID_PLATFORM; they must reach the SDK builds or those configure
    # for the build host's architecture.
    list(APPEND GRACE_SDK_CMAKE_ARGS
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM})
endif()
# The adapter targets reference the prefix's include/lib directories before
# the ExternalProject builds run; the directories must exist at configure
# time, or the configure step fails on the imported target's paths.
file(MAKE_DIRECTORY "${GRACE_THIRD_PARTY}/include" "${GRACE_THIRD_PARTY}/lib")

# ---------------------------------------------------------------------------
# The Ogg chain: libogg and libvorbis (the audio decode layer links
# libvorbisfile, with libvorbis and libogg), and libtheora when the Theora
# option is on (theora's configure step finds the ogg headers through the
# pkg-config files of the built install prefix, and the chain is built in
# dependency order).
# ---------------------------------------------------------------------------

if(GRACE_ENABLE_THEORA OR GRACE_ENABLE_OPENAL)
ExternalProject_Add(grace_libogg
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libogg
    CMAKE_ARGS ${GRACE_SDK_CMAKE_ARGS}
    BUILD_BYPRODUCTS ${GRACE_THIRD_PARTY}/lib/libogg.a)

ExternalProject_Add(grace_libvorbis
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libvorbis
    CMAKE_ARGS ${GRACE_SDK_CMAKE_ARGS}
    BUILD_BYPRODUCTS ${GRACE_THIRD_PARTY}/lib/libvorbisfile.a
        ${GRACE_THIRD_PARTY}/lib/libvorbis.a)

if(GRACE_ENABLE_THEORA)
ExternalProject_Add(grace_libtheora
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libtheora
    DEPENDS grace_libogg
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        PKG_CONFIG_PATH=${GRACE_THIRD_PARTY}/lib/pkgconfig
        <SOURCE_DIR>/autogen.sh --prefix=<INSTALL_DIR>
        --disable-examples --disable-spec --disable-encexample
        --disable-telemetry --disable-oggtest --disable-vorbistest
    BUILD_COMMAND make -j4
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE 1
    INSTALL_DIR ${GRACE_THIRD_PARTY}
    BUILD_BYPRODUCTS ${GRACE_THIRD_PARTY}/lib/libtheoradec.a)
endif()
endif()

# ---------------------------------------------------------------------------
# SDL2 (static; the OpenGL, the Vulkan and the Metal backends link it)
# ---------------------------------------------------------------------------

if(GRACE_ENABLE_OPENGL OR GRACE_ENABLE_VULKAN OR GRACE_ENABLE_METAL)
ExternalProject_Add(grace_sdl2
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sdl2
    CMAKE_ARGS ${GRACE_SDK_CMAKE_ARGS}
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF
        -DSDL_AUDIO=OFF -DSDL_RENDER=OFF -DSDL_EVENTS=ON
        -DSDL_POWER=OFF
        -DSDL_FILESYSTEM=OFF -DSDL_TIMERS=ON
        -DSDL_THREADS=ON
    BUILD_BYPRODUCTS ${GRACE_THIRD_PARTY}/lib/libSDL2.a)
endif()

# ---------------------------------------------------------------------------
# OpenAL Soft (static; the audio backend links it)
# ---------------------------------------------------------------------------

if(GRACE_ENABLE_OPENAL)
ExternalProject_Add(grace_openal
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/openal-soft
    CMAKE_ARGS ${GRACE_SDK_CMAKE_ARGS}
        -DLIBTYPE=STATIC
    BUILD_BYPRODUCTS ${GRACE_THIRD_PARTY}/lib/libopenal.a)
endif()

# ---------------------------------------------------------------------------
# FreeType (static; the GUI font adapter links it)
# ---------------------------------------------------------------------------

if(GRACE_ENABLE_FREETYPE)
ExternalProject_Add(grace_freetype
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/freetype
    CMAKE_ARGS ${GRACE_SDK_CMAKE_ARGS}
        -DBUILD_SHARED_LIBS=OFF
        -DFT_DISABLE_PNG=ON -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
    BUILD_BYPRODUCTS ${GRACE_THIRD_PARTY}/lib/libfreetype.a)
endif()

# ---------------------------------------------------------------------------
# The physics and script SDKs (the adapters link the static libraries)
# ---------------------------------------------------------------------------

if(GRACE_ENABLE_BULLET)
ExternalProject_Add(grace_bullet
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bullet3
    CMAKE_ARGS ${GRACE_SDK_CMAKE_ARGS}
        -DUSE_DOUBLE_PRECISION=OFF -DUSE_GRAPHICAL_BENCHMARK=OFF
        -DBUILD_SHARED_LIBS=OFF -DBUILD_BULLET3=OFF
        -DBUILD_BULLET2_DEMOS=OFF -DBUILD_OPENGL3_DEMOS=OFF
        -DBUILD_EXTRAS=OFF -DBUILD_UNIT_TESTS=OFF
        -DINSTALL_LIBS=OFF
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/src/BulletDynamics/libBulletDynamics.a
        <BINARY_DIR>/src/BulletCollision/libBulletCollision.a
        <BINARY_DIR>/src/LinearMath/libLinearMath.a
        ${GRACE_THIRD_PARTY}/lib/
    BUILD_BYPRODUCTS
        <BINARY_DIR>/src/BulletDynamics/libBulletDynamics.a
        <BINARY_DIR>/src/BulletCollision/libBulletCollision.a
        <BINARY_DIR>/src/LinearMath/libLinearMath.a)
endif()

if(GRACE_ENABLE_BOX2D)
ExternalProject_Add(grace_box2d
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/box2d
    CMAKE_ARGS ${GRACE_SDK_CMAKE_ARGS}
        -DBOX2D_SAMPLES=OFF -DBOX2D_UNIT_TESTS=OFF -DBOX2D_BENCHMARKS=OFF
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/src/libbox2d.a ${GRACE_THIRD_PARTY}/lib/
    BUILD_BYPRODUCTS <BINARY_DIR>/src/libbox2d.a)
endif()

if(GRACE_ENABLE_ANGELSCRIPT)
ExternalProject_Add(grace_angelscript
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/angelscript/sdk/angelscript/projects/cmake
    CMAKE_ARGS ${GRACE_SDK_CMAKE_ARGS}
        -DBUILD_SHARED_LIBS=OFF
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/libangelscript.a ${GRACE_THIRD_PARTY}/lib/
    BUILD_BYPRODUCTS <BINARY_DIR>/libangelscript.a)
endif()

# ---------------------------------------------------------------------------
# PhysX 3.4 (the SDK's own Makefiles; the checkout is patched in place by the
# tw2-proven pattern: the makefiles ship with -Werror, which modern GCC
# rejects, and one header needs a const-correctness fix)
# ---------------------------------------------------------------------------

if(GRACE_ENABLE_PHYSX)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR
            "GRACE_ENABLE_PHYSX=ON is supported on Linux only "
            "(the adapter links the SDK's linux64 Makefiles build; the "
            "Windows validation milestone 17 does not cover the adapter)")
    endif()
ExternalProject_Add(grace_physx
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Source/compiler/linux64
    CONFIGURE_COMMAND ""
    PATCH_COMMAND ${CMAKE_COMMAND} -DSDK_DIR=${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/physx-patch.cmake
    BUILD_COMMAND make -j4 release
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libPhysX3Extensions.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libSimulationController.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libSceneQuery.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libLowLevelDynamics.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libLowLevelAABB.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libLowLevel.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Bin/linux64/libPhysX3_x64.so
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Bin/linux64/libPhysX3Common_x64.so
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PxShared/bin/linux64/libPxFoundation_x64.so
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PxShared/lib/linux64/libPxTask.a)
endif()

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
if(MSVC)
    target_compile_options(grace_warnings INTERFACE
        /W4
        /permissive-)
    if(GRACE_WARNINGS_AS_ERRORS)
        target_compile_options(grace_warnings INTERFACE /WX)
    endif()
endif()
if(WIN32 AND NOT MSVC)
    # The default MinGW build links the GCC runtime as DLLs; the engine ships
    # no DLLs, so the runtime is linked in statically. Visual Studio links the
    # runtime statically by default and needs no equivalent.
    target_link_options(grace_warnings INTERFACE
        -static-libgcc -static-libstdc++ -static)
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
    src/core/fixed_step_clock.cpp
    src/core/curves.cpp
    src/core/tasks.cpp
    src/core/profiler.cpp
    src/core/tween.cpp
    src/core/compressed_textures.cpp)

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
    add_dependencies(grace_angelscript_script grace_angelscript)
    target_include_directories(grace_angelscript_script SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/angelscript/sdk/angelscript/include)
    target_link_libraries(grace_angelscript_script PUBLIC grace_script
        PRIVATE grace_warnings
        ${GRACE_THIRD_PARTY}/lib/libangelscript.a)
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
# option. The SDKs are the pinned git submodules; CMake builds them first
# (add_dependencies) and the adapter links the built libraries.
# ---------------------------------------------------------------------------

if(GRACE_ENABLE_PHYSX)
    add_library(grace_physx_physics STATIC
        src/physics/physx_physics_backend.cpp)
    add_dependencies(grace_physx_physics grace_physx)
    target_include_directories(grace_physx_physics SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Include
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PxShared/include)
    target_link_libraries(grace_physx_physics PUBLIC grace_physics
        PRIVATE grace_warnings
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libPhysX3Extensions.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libSimulationController.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libSceneQuery.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libLowLevelDynamics.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libLowLevelAABB.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Lib/linux64/libLowLevel.a
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Bin/linux64/libPhysX3_x64.so
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PhysX_3.4/Bin/linux64/libPhysX3Common_x64.so
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PxShared/bin/linux64/libPxFoundation_x64.so
        ${CMAKE_CURRENT_SOURCE_DIR}/physx-3.4/PxShared/lib/linux64/libPxTask.a)
endif()

if(GRACE_ENABLE_BULLET)
    add_library(grace_bullet_physics STATIC
        src/physics/bullet_physics_backend.cpp)
    add_dependencies(grace_bullet_physics grace_bullet)
    target_include_directories(grace_bullet_physics SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/bullet3/src)
    target_link_libraries(grace_bullet_physics PUBLIC grace_physics
        PRIVATE grace_warnings
        ${GRACE_THIRD_PARTY}/lib/libBulletDynamics.a
        ${GRACE_THIRD_PARTY}/lib/libBulletCollision.a
        ${GRACE_THIRD_PARTY}/lib/libLinearMath.a)
endif()

if(GRACE_ENABLE_BOX2D)
    add_library(grace_box2d_physics STATIC
        src/physics/box2d_physics_backend.cpp)
    add_dependencies(grace_box2d_physics grace_box2d)
    target_include_directories(grace_box2d_physics SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/box2d/include)
    target_link_libraries(grace_box2d_physics PUBLIC grace_physics
        PRIVATE grace_warnings
        ${GRACE_THIRD_PARTY}/lib/libbox2d.a)
endif()

# ---------------------------------------------------------------------------
# SDL2
#
# SDL2 comes from the pinned git submodule (grace_sdl2 above), built as
# a static library into the third-party prefix. Both the OpenGL and the
# Vulkan backends consume it through the same imported target.
# ---------------------------------------------------------------------------
if(GRACE_ENABLE_OPENGL OR GRACE_ENABLE_VULKAN OR GRACE_ENABLE_METAL)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    target_include_directories(SDL2::SDL2 SYSTEM INTERFACE
        ${GRACE_THIRD_PARTY}/include)
    target_link_libraries(SDL2::SDL2 INTERFACE
        ${GRACE_THIRD_PARTY}/lib/libSDL2.a)
    if(WIN32)
        # A static SDL2 on Windows needs the Windows API libraries that
        # the SDL2 CMake build records as its own dependencies; CMake
        # resolves them at the engine's link.
        target_link_libraries(SDL2::SDL2 INTERFACE
            user32 gdi32 winmm imm32 ole32 oleaut32 version uuid
            advapi32 setupapi shell32)
    elseif(APPLE)
        # A static SDL2 on macOS needs the Apple frameworks that the SDL2
        # CMake build records as its own dependencies; the SDL2::SDL2
        # imported target it exports does not carry that list, so the
        # engine's link would fail with undefined NSWindow/IOHID symbols.
        target_link_libraries(SDL2::SDL2 INTERFACE
            "-Wl,-framework,Cocoa" "-Wl,-framework,Carbon"
            "-Wl,-framework,Foundation" "-Wl,-framework,CoreVideo"
            "-Wl,-framework,CoreAudio" "-Wl,-framework,AudioToolbox"
            "-Wl,-framework,AVFoundation" "-Wl,-framework,IOKit"
            "-Wl,-framework,ForceFeedback"
            "-Wl,-weak_framework,Metal" "-Wl,-weak_framework,QuartzCore"
            "-Wl,-weak_framework,GameController" "-Wl,-weak_framework,CoreHaptics")
    endif()
endif()

# ---------------------------------------------------------------------------
# OpenGL render backend
#
# SDL2 provides window/context/input; OpenGL comes from GLVND
# (OpenGL::OpenGL, never the legacy libGL) on Linux and from opengl32.dll
# on Windows. Entry points beyond GL 1.1 are resolved at runtime through
# SDL_GL_GetProcAddress by the internal loader in this module.
# ---------------------------------------------------------------------------
if(GRACE_ENABLE_OPENGL)
    set(OpenGL_GL_PREFERENCE GLVND)
    if(WIN32)
        # Windows: OpenGL 1.1 comes from opengl32.dll, the platform SDK's
        # import library; entry points beyond GL 1.1 are resolved at
        # runtime (SDL_GL_GetProcAddress) exactly as on Linux.
        target_link_libraries(SDL2::SDL2 INTERFACE opengl32)
    else()
        set(OpenGL_GL_PREFERENCE GLVND)
        find_package(OpenGL REQUIRED)
    endif()

    add_library(grace_render_opengl STATIC
        src/render/opengl/gl_loader.cpp
        src/render/opengl/opengl_mesh.cpp
        src/render/opengl/opengl_shader.cpp
        src/render/opengl/opengl_render_target.cpp
        src/render/opengl/opengl_render_backend.cpp)
    add_dependencies(grace_render_opengl grace_sdl2)

    if(WIN32)
        # OpenGL::OpenGL does not exist on Windows (GLVND is a Linux
        # system package); the GL 1.1 import library comes through
        # SDL2::SDL2 above.
        target_link_libraries(grace_render_opengl
            PUBLIC grace_render
            PRIVATE grace_warnings SDL2::SDL2)
    else()
        target_link_libraries(grace_render_opengl
            PUBLIC grace_render
            PRIVATE grace_warnings SDL2::SDL2 OpenGL::OpenGL)
    endif()
endif()

# ---------------------------------------------------------------------------
# Vulkan render backend
#
# SDL2 provides window/input; the Vulkan entry points are resolved at runtime
# through SDL_Vulkan_GetVkGetInstanceProcAddr by the internal loader in this
# module (no link-time Vulkan dependency, the same rule the OpenGL backend
# follows). The shader library's GLSL 450 sources are compiled to SPIR-V at
# build time by glslangValidator (the build-time prerequisite, decision 44),
# and the generated header is embedded into the module.
# ---------------------------------------------------------------------------

if(GRACE_ENABLE_VULKAN)
    # The SPIR-V step: every engine shader in vulkan_shader_library.hpp
    # compiles to SPIR-V at build time; the generated header carries the
    # per-shader byte arrays. The generator runs glslangValidator through
    # the CMake command (the tool is a build-time prerequisite).
    find_program(GRACE_GLSLANG_VALIDATOR glslangValidator
        REQUIRED)
    set(GRACE_SPIRV_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/spirv")
    file(MAKE_DIRECTORY "${GRACE_SPIRV_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/build-spirv.py
            ${CMAKE_CURRENT_SOURCE_DIR} ${GRACE_SPIRV_DIR}
            ${GRACE_GLSLANG_VALIDATOR}
        RESULT_VARIABLE GRACE_SPIRV_RESULT
        OUTPUT_VARIABLE GRACE_SPIRV_OUTPUT
        ERROR_VARIABLE GRACE_SPIRV_OUTPUT)
    if(NOT GRACE_SPIRV_RESULT EQUAL 0)
        message(FATAL_ERROR "the Vulkan backend's SPIR-V step failed:\n"
            "${GRACE_SPIRV_OUTPUT}")
    endif()

    add_library(grace_render_vulkan STATIC
        src/render/vulkan/vk_loader.cpp
        src/render/vulkan/vulkan_shader.cpp
        src/render/vulkan/vulkan_mesh.cpp
        src/render/vulkan/vulkan_render_target.cpp
        src/render/vulkan/vulkan_render_backend.cpp)
    target_include_directories(grace_render_vulkan PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    target_include_directories(grace_render_vulkan PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render/vulkan)
    target_include_directories(grace_render_vulkan PRIVATE
        ${GRACE_SPIRV_DIR})
    add_dependencies(grace_render_vulkan grace_sdl2)
    target_link_libraries(grace_render_vulkan
        PUBLIC grace_render
        PRIVATE grace_warnings SDL2::SDL2)
if(ANDROID)
        # SDL2's Android backend references the NDK's platform libraries
        # (the native window, the log and the sensor manager).
        target_link_libraries(grace_render_vulkan PRIVATE android log)
    endif()
endif()

# ---------------------------------------------------------------------------
# Metal render backend
#
# SDL2 provides window/input (the CAMetalLayer comes from SDL's Metal
# bridge); the Objective-C runtime and the Metal framework's entry
# points are resolved at runtime by the internal loader in this module
# (no link-time dependency, no Apple SDK header in the tree — the loader
# declares its own types, so the module compiles on every platform and
# the mapping tests run anywhere; the hardware run needs a Mac).
# There is no build-time shader step: MSL compiles at runtime through
# MTLLibrary, so glslangValidator has no Metal equivalent.
# ---------------------------------------------------------------------------
if(GRACE_ENABLE_METAL)
    add_library(grace_render_metal STATIC
        src/render/metal/mtl_loader.cpp
        src/render/metal/metal_shader.cpp
        src/render/metal/metal_mesh.cpp
        src/render/metal/metal_render_target.cpp
        src/render/metal/metal_render_backend.cpp)
    target_include_directories(grace_render_metal PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    target_include_directories(grace_render_metal PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render/metal)
    add_dependencies(grace_render_metal grace_sdl2)
    target_link_libraries(grace_render_metal
        PUBLIC grace_render
        PRIVATE grace_warnings SDL2::SDL2)
    if(APPLE)
        # The Objective-C runtime is part of the system; the framework
        # loads at runtime (the loader's dlopen), the link-time list
        # stays empty.
        target_link_libraries(grace_render_metal PRIVATE objc)
    endif()
    if(ANDROID)
        # SDL2's Android backend references the NDK's platform libraries
        # (the native window, the log and the sensor manager).
        target_link_libraries(grace_render_metal PRIVATE android log)
    endif()
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
# grace_audio
#
# The audio contract (docs/grace-architecture.md section 3.10): buffers,
# voices, streams, per-sound reverb and the 3D listener. The null backend is
# the deterministic mixer, the protocol reference; the OpenAL Soft adapter is
# built only when its SDK option is on. The decode layer (WAV, Ogg Vorbis)
# lives in the data layer, never in the backend calls.
# ---------------------------------------------------------------------------
add_library(grace_audio STATIC
    src/audio/audio_types.cpp
    src/audio/audio_decode.cpp
    src/audio/null_audio_backend.cpp)
target_include_directories(grace_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_audio PUBLIC grace_core PRIVATE grace_warnings)

if(GRACE_ENABLE_OPENAL)
    add_library(grace_openal_audio STATIC
        src/audio/openal_audio_backend.cpp
        src/audio/audio_decode_vorbis.cpp)
    target_include_directories(grace_openal_audio PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    add_dependencies(grace_openal_audio grace_openal grace_libvorbis)
    add_library(OpenAL::OpenAL INTERFACE IMPORTED)
    target_include_directories(OpenAL::OpenAL SYSTEM INTERFACE
        ${GRACE_THIRD_PARTY}/include
        ${GRACE_THIRD_PARTY}/include/AL)
    target_link_libraries(OpenAL::OpenAL INTERFACE
        ${GRACE_THIRD_PARTY}/lib/libopenal.a)
    target_link_libraries(grace_openal_audio PUBLIC grace_audio
        PRIVATE grace_warnings OpenAL::OpenAL ${VORBISFILE_LIBRARIES})
    target_include_directories(grace_openal_audio SYSTEM PRIVATE
        ${GRACE_THIRD_PARTY}/include)
    target_link_libraries(grace_openal_audio PRIVATE
        ${GRACE_THIRD_PARTY}/lib/libvorbisfile.a
        ${GRACE_THIRD_PARTY}/lib/libvorbis.a
        ${GRACE_THIRD_PARTY}/lib/libogg.a)
endif()

if(GRACE_ENABLE_XAUDIO2 AND WIN32)
    add_library(grace_xaudio2_audio STATIC
        src/audio/xaudio2_audio_backend.cpp)
    target_include_directories(grace_xaudio2_audio PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    target_link_libraries(grace_xaudio2_audio PUBLIC grace_audio
        PRIVATE grace_warnings xaudio2_8)
endif()

if(GRACE_ENABLE_OPENSL AND ANDROID)
    add_library(grace_opensl_audio STATIC
        src/audio/opensl_audio_backend.cpp)
    target_include_directories(grace_opensl_audio PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    target_link_libraries(grace_opensl_audio PUBLIC grace_audio
        PRIVATE grace_warnings OpenSLES)
endif()

# ---------------------------------------------------------------------------
# grace_motion
#
# The motion module (docs/grace-architecture.md section 3.7): skeletal
# keyframe clips, the deterministic sampler, the blendshapes, the state
# machine layer and the node controllers. Plain data and deterministic CPU
# work; the skinning stays hardware-side (the render module owns the skin
# matrices).
# ---------------------------------------------------------------------------
add_library(grace_motion STATIC
    src/motion/motion_types.cpp
    src/motion/motion_sampler.cpp
    src/motion/motion_machine.cpp
    src/motion/motion_controllers.cpp)
target_include_directories(grace_motion PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_motion PUBLIC grace_core PRIVATE grace_warnings)
# ---------------------------------------------------------------------------
# grace_gui
#
# The GUI module (docs/grace-architecture.md section 3.9): the retained
# control tree, the hit test and focus, the event dispatch with the UTF-8
# text path, and the font contract. The null backend is the deterministic
# reference; the FreeType adapter is built only when its SDK option is on.
# ---------------------------------------------------------------------------
add_library(grace_gui STATIC
    src/gui/gui_types.cpp
    src/gui/null_gui_backend.cpp)
target_include_directories(grace_gui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_gui PUBLIC grace_core PRIVATE grace_warnings)
if(GRACE_ENABLE_FREETYPE)
    add_library(grace_freetype_gui STATIC
        src/gui/freetype_gui_backend.cpp)
    add_dependencies(grace_freetype_gui grace_freetype)
    target_include_directories(grace_freetype_gui PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    target_include_directories(grace_freetype_gui SYSTEM PRIVATE
        ${GRACE_THIRD_PARTY}/include/freetype2)
    target_link_libraries(grace_freetype_gui PUBLIC grace_gui
        PRIVATE grace_warnings ${GRACE_THIRD_PARTY}/lib/libfreetype.a)
endif()
# ---------------------------------------------------------------------------
# grace_game
#
# The game module (docs/grace-architecture.md section 2's module table):
# the agent behaviour system (AI is agent behaviour) and the
# gameplay-level services (Game). The steering, the timers, the
# properties, the event queue and the random source are deterministic
# CPU work; the random source is a xorshift64* generator the application
# seeds explicitly.
# ---------------------------------------------------------------------------
add_library(grace_game STATIC
    src/game/game_types.cpp
    src/game/game.cpp)
target_include_directories(grace_game PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_game PUBLIC grace_core PRIVATE grace_warnings)
# ---------------------------------------------------------------------------
# grace_video
#
# The video module (docs/grace-architecture.md section 2's module table):
# the playback service. The contract is the frame stepper; the null
# backend is the deterministic reference; the Theora decode layer is
# built only when its SDK option is on.
# ---------------------------------------------------------------------------
add_library(grace_video STATIC
    src/video/video_types.cpp
    src/video/video_decode.cpp
    src/video/null_video_backend.cpp)
target_include_directories(grace_video PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(grace_video PUBLIC grace_core PRIVATE grace_warnings)

if(GRACE_ENABLE_THEORA)
    add_library(grace_theora_video STATIC
        src/video/video_decode_theora.cpp)
    add_dependencies(grace_theora_video grace_libtheora grace_libvorbis
        grace_libogg)
    target_include_directories(grace_theora_video PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    target_include_directories(grace_theora_video SYSTEM PRIVATE
        ${GRACE_THIRD_PARTY}/include)
    target_link_libraries(grace_theora_video PUBLIC grace_video
        PRIVATE grace_warnings
        ${GRACE_THIRD_PARTY}/lib/libtheoradec.a
        ${GRACE_THIRD_PARTY}/lib/libogg.a)
endif()

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

if(GRACE_ENABLE_VULKAN)
    target_link_libraries(grace PRIVATE grace_render_vulkan)
    target_compile_definitions(grace PRIVATE GRACE_HAVE_VULKAN=1)
endif()

if(GRACE_ENABLE_METAL)
    target_link_libraries(grace PRIVATE grace_render_metal)
    target_compile_definitions(grace PRIVATE GRACE_HAVE_METAL=1)
endif()

# ---------------------------------------------------------------------------
# The Android app shell
#
# A bare executable cannot initialise SDL on Android: SDL_Init needs an
# Activity with a JNI environment and an ANativeWindow (decision 52). The
# shell is the same main.cpp compiled as a shared library with SDL_main.h
# forced into the translation unit, so the entry point becomes SDL_main and
# SDLActivity loads libmain.so from the APK; the static SDL2 carries the
# JNI_OnLoad that registers the native methods. The same sources build the
# Linux, Windows and macOS executables and this shared library.
# ---------------------------------------------------------------------------
if(GRACE_ENABLE_ANDROID_SHELL)
    if(NOT ANDROID)
        message(FATAL_ERROR
            "GRACE_ENABLE_ANDROID_SHELL=ON is supported on Android only "
            "(the shared library is what SDLActivity loads from the APK)")
    endif()
    if(NOT (GRACE_ENABLE_OPENGL OR GRACE_ENABLE_VULKAN
            OR GRACE_ENABLE_METAL))
        message(FATAL_ERROR
            "GRACE_ENABLE_ANDROID_SHELL=ON needs a render backend: the "
            "shell exists to put a window and a graphics API behind the "
            "engine on the device")
    endif()
    add_library(grace_main SHARED
        src/app/main.cpp)
    set_target_properties(grace_main PROPERTIES OUTPUT_NAME main)
    # SDL_main.h redefines main as SDL_main on Android; forcing the header
    # into the translation unit renames the entry point without touching the
    # source, and the header's SDLMAIN_DECLSPEC exports the symbol.
    target_compile_options(grace_main PRIVATE
        -include SDL2/SDL_main.h)
    target_include_directories(grace_main PRIVATE
        ${GRACE_THIRD_PARTY}/include)
    target_link_libraries(grace_main PRIVATE
        grace_app grace_null_render grace_warnings)
    if(GRACE_ENABLE_OPENGL)
        target_link_libraries(grace_main PRIVATE grace_render_opengl)
        target_compile_definitions(grace_main PRIVATE GRACE_HAVE_OPENGL=1)
    endif()
    if(GRACE_ENABLE_VULKAN)
        target_link_libraries(grace_main PRIVATE grace_render_vulkan)
        target_compile_definitions(grace_main PRIVATE GRACE_HAVE_VULKAN=1)
    endif()
    if(GRACE_ENABLE_METAL)
        target_link_libraries(grace_main PRIVATE grace_render_metal)
        target_compile_definitions(grace_main PRIVATE GRACE_HAVE_METAL=1)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(GRACE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
