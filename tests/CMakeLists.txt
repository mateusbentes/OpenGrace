# One executable per suite keeps failures attributable without a test
# framework. Each suite prints its own check count and returns non-zero when
# any check fails.

add_executable(test_version test_version.cpp)

target_link_libraries(test_version PRIVATE grace_core grace_warnings)
target_include_directories(test_version PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME version COMMAND test_version)

add_executable(test_fixed_step_clock test_fixed_step_clock.cpp)

target_link_libraries(test_fixed_step_clock PRIVATE grace_core grace_warnings)
target_include_directories(test_fixed_step_clock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME fixed_step_clock COMMAND test_fixed_step_clock)

add_executable(test_application test_application.cpp)

target_link_libraries(test_application PRIVATE grace_app grace_null_render grace_warnings)
target_include_directories(test_application PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME application COMMAND test_application)

add_executable(test_math test_math.cpp)

target_link_libraries(test_math PRIVATE grace_core grace_warnings)
target_include_directories(test_math PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME math COMMAND test_math)

# Milestone 21: the core-completion subsystems. Curves (21a) evaluate
# deterministically; the task scheduler (21b) proves result determinism on
# a thread pool; the profiler (21c) counts zones and violations; the
# compressed-texture decoder (21d) decodes BC1-BC5 bit-exactly on the CPU.
add_executable(test_curves test_curves.cpp)
target_link_libraries(test_curves PRIVATE grace_core grace_warnings)
target_include_directories(test_curves PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME curves COMMAND test_curves)

# Milestone 23: the procedural tween. The easing endpoints are exact, the
# interpolation matches the closed-form polynomials, and a zero or negative
# duration is rejected like the other subsystems reject invalid arguments.
add_executable(test_tween test_tween.cpp)
target_link_libraries(test_tween PRIVATE grace_core grace_warnings)
target_include_directories(test_tween PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME tween COMMAND test_tween)

add_executable(test_tasks test_tasks.cpp)
target_link_libraries(test_tasks PRIVATE grace_core grace_warnings)
target_include_directories(test_tasks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME tasks COMMAND test_tasks)

add_executable(test_profiler test_profiler.cpp)
target_link_libraries(test_profiler PRIVATE grace_core grace_warnings)
target_include_directories(test_profiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME profiler COMMAND test_profiler)

add_executable(test_compressed_textures test_compressed_textures.cpp)
target_link_libraries(test_compressed_textures PRIVATE grace_core grace_warnings)
target_include_directories(test_compressed_textures PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME compressed_textures COMMAND test_compressed_textures)

# Physics contract (8a) and null backend (8b): deterministic manual
# integration, no physics SDK.
add_executable(test_physics test_physics.cpp)
target_link_libraries(test_physics PRIVATE grace_physics grace_null_physics grace_warnings)
target_include_directories(test_physics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME physics COMMAND test_physics)

# Script host (9a): the contract and the null backend, the protocol
# reference. No scripting-language dependency.
add_executable(test_script test_script.cpp)
target_link_libraries(test_script PRIVATE grace_script grace_null_script grace_warnings)
target_include_directories(test_script PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME script COMMAND test_script)

# Visual scripting (10a): the contract and the null backend, the protocol
# reference. No scripting-language dependency.
add_executable(test_visual test_visual.cpp)
target_link_libraries(test_visual PRIVATE grace_visual grace_null_visual grace_warnings)
target_include_directories(test_visual PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME visual COMMAND test_visual)

# Audio (13a): the contract and the null backend, the protocol reference.
# No audio SDK: the null backend is the deterministic mixer.
add_executable(test_game test_game.cpp)
target_link_libraries(test_game PRIVATE grace_game grace_warnings)
target_include_directories(test_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME game COMMAND test_game)

add_executable(test_gui test_gui.cpp)
target_link_libraries(test_gui PRIVATE grace_gui grace_warnings)
target_include_directories(test_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_video test_video.cpp)
target_link_libraries(test_video PRIVATE grace_video grace_warnings)
target_include_directories(test_video PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME video COMMAND test_video)

add_test(NAME gui COMMAND test_gui)

add_executable(test_motion test_motion.cpp)
target_link_libraries(test_motion PRIVATE grace_motion grace_warnings)
target_include_directories(test_motion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME motion COMMAND test_motion)

add_executable(test_audio test_audio.cpp)
target_link_libraries(test_audio PRIVATE grace_audio grace_warnings)
target_include_directories(test_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME audio COMMAND test_audio)

# Audio (13c): the OpenAL Soft adapter and the decode layer. It is only
# built when its SDK option is on, so this suite exists only when the SDK
# is present (the SDK lives outside the repo).
if(GRACE_ENABLE_OPENAL)
    add_executable(test_audio_adapters test_audio_adapters.cpp)
    target_link_libraries(test_audio_adapters PRIVATE
        grace_audio grace_openal_audio grace_warnings)
    target_include_directories(test_audio_adapters PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME audio_adapters COMMAND test_audio_adapters)
    set_tests_properties(audio_adapters PROPERTIES
        SKIP_RETURN_CODE 77)
endif()
if(GRACE_ENABLE_XAUDIO2 AND WIN32)
    add_executable(test_audio_xaudio2 test_audio_xaudio2.cpp)
    target_link_libraries(test_audio_xaudio2 PRIVATE
        grace_audio grace_xaudio2_audio grace_warnings)
    target_include_directories(test_audio_xaudio2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME audio_xaudio2 COMMAND test_audio_xaudio2)
    set_tests_properties(audio_xaudio2 PROPERTIES
        SKIP_RETURN_CODE 77)
endif()
if(GRACE_ENABLE_OPENSL AND ANDROID)
    add_executable(test_audio_opensl test_audio_opensl.cpp)
    target_link_libraries(test_audio_opensl PRIVATE
        grace_audio grace_opensl_audio grace_warnings)
    target_include_directories(test_audio_opensl PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME audio_opensl COMMAND test_audio_opensl)
    set_tests_properties(audio_opensl PROPERTIES
        SKIP_RETURN_CODE 77)
endif()
if(GRACE_ENABLE_FREETYPE)
    add_executable(test_gui_adapters test_gui_adapters.cpp)
    target_link_libraries(test_gui_adapters PRIVATE
        grace_freetype_gui grace_warnings)
    target_include_directories(test_gui_adapters PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME gui_adapters COMMAND test_gui_adapters)
    set_tests_properties(gui_adapters PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(GRACE_ENABLE_THEORA)
    add_executable(test_video_adapters test_video_adapters.cpp)
    target_link_libraries(test_video_adapters PRIVATE
        grace_theora_video grace_warnings)
    target_include_directories(test_video_adapters PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME video_adapters COMMAND test_video_adapters)
endif()

# Runtime tests for the real backends (8c). Each adapter target is only
# built when its SDK option is on, so this suite exists only when all of
# them are (the sandbox build is; the SDKs live outside the repo).
if(GRACE_ENABLE_PHYSX AND GRACE_ENABLE_BULLET AND GRACE_ENABLE_BOX2D)
    add_executable(test_physics_adapters test_physics_adapters.cpp)
    target_link_libraries(test_physics_adapters PRIVATE
        grace_physics grace_null_physics grace_bullet_physics
        grace_box2d_physics grace_physx_physics grace_warnings)
    target_include_directories(test_physics_adapters PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME physics_adapters COMMAND test_physics_adapters)
    set_tests_properties(physics_adapters PROPERTIES ENVIRONMENT
        "LD_LIBRARY_PATH=${CMAKE_SOURCE_DIR}/physx-3.4/PhysX_3.4/Bin/linux64:${CMAKE_SOURCE_DIR}/physx-3.4/PxShared/bin/linux64")
endif()

# Script host (9b): the AngelScript adapter, the execution reference. It is
# only built when its SDK option is on, so this suite exists only when the
# SDK is present (the SDK lives outside the repo).
if(GRACE_ENABLE_ANGELSCRIPT)
    add_executable(test_script_adapters test_script_adapters.cpp)
    target_link_libraries(test_script_adapters PRIVATE
        grace_script grace_null_script grace_angelscript_script grace_warnings)
    target_include_directories(test_script_adapters PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME script_adapters COMMAND test_script_adapters)

    add_executable(test_visual_adapters test_visual_adapters.cpp)
    target_link_libraries(test_visual_adapters PRIVATE
        grace_visual grace_null_visual grace_angelscript_script
        grace_angelscript_visual grace_warnings)
    target_include_directories(test_visual_adapters PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME visual_adapters COMMAND test_visual_adapters)
endif()

add_executable(test_lights test_lights.cpp)

target_link_libraries(test_lights PRIVATE grace_render grace_null_render grace_warnings)
target_include_directories(test_lights PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME lights COMMAND test_lights)

# 3D texture contract checks (milestone 7, first increment). Linked against the
# null backend only, so it runs with no GL context: the volume capability is
# proved where it can be proved everywhere. The OpenGL half of the same checks
# runs inside test_opengl_render through the shared runTexture3DChecks.
add_executable(test_texture3d test_texture3d.cpp)

target_link_libraries(test_texture3d PRIVATE grace_render grace_null_render grace_warnings)
target_include_directories(test_texture3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME texture3d COMMAND test_texture3d)

add_executable(test_cascades test_cascades.cpp)

target_link_libraries(test_cascades PRIVATE grace_render grace_null_render grace_warnings)
target_include_directories(test_cascades PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME cascades COMMAND test_cascades)

add_executable(test_paraboloid test_paraboloid.cpp)

target_link_libraries(test_paraboloid PRIVATE grace_render grace_warnings)
target_include_directories(test_paraboloid PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME paraboloid COMMAND test_paraboloid)

add_executable(test_trapezoid test_trapezoid.cpp)

target_link_libraries(test_trapezoid PRIVATE grace_render grace_warnings)
target_include_directories(test_trapezoid PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME trapezoid COMMAND test_trapezoid)

add_executable(test_shader_source_rule test_shader_source_rule.cpp)
target_link_libraries(test_shader_source_rule PRIVATE grace_warnings)
target_include_directories(test_shader_source_rule PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME shader_source_rule COMMAND test_shader_source_rule ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(test_renderer test_renderer.cpp)

target_link_libraries(test_renderer PRIVATE grace_render grace_null_render grace_warnings)
target_include_directories(test_renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME renderer COMMAND test_renderer)

add_executable(test_docs_structure test_docs_structure.cpp)
target_link_libraries(test_docs_structure PRIVATE grace_warnings)
target_include_directories(test_docs_structure PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME docs_structure COMMAND test_docs_structure ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The OWD container (11): the writer, the reader, the filters, the checksums
# and the overlay resolution. zlib's Deflate comes through grace_owd.
add_executable(test_owd test_owd.cpp)
target_link_libraries(test_owd PRIVATE grace_owd grace_warnings)
target_include_directories(test_owd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME owd COMMAND test_owd)

# Scene management (12a, 12b): the entities, the partitioning, the queries
# and the LOD, plus the serialisation strategy (decision 36) writing into
# the OWD container.
add_executable(test_scene test_scene.cpp)
target_link_libraries(test_scene PRIVATE grace_scene grace_owd grace_warnings)
target_include_directories(test_scene PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME scene COMMAND test_scene)

# CLI tests for the grace executable, checked for their exact exit code
# through check_exit_code.cmake in script mode (no test framework):
# 0 = success, 2 = usage error.
set(GRACE_EXE $<TARGET_FILE:grace>)
set(GRACE_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}")
add_test(NAME app_headless_run COMMAND ${CMAKE_COMMAND}
    -DEXPECTED_EXIT_CODE=0
    -DEXECUTABLE=${GRACE_EXE}
    -DEMULATOR=${GRACE_EMULATOR}
    "-DARGS=--headless;--frames;4;--frame-time-ns;20000000;--step-ns;10000000;--width;320;--height;240"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exit_code.cmake)
add_test(NAME app_usage_error_unknown_option COMMAND ${CMAKE_COMMAND}
    -DEXPECTED_EXIT_CODE=2
    -DEXECUTABLE=${GRACE_EXE}
    -DEMULATOR=${GRACE_EMULATOR}
    "-DARGS=--no-such-option"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exit_code.cmake)
add_test(NAME app_usage_error_missing_value COMMAND ${CMAKE_COMMAND}
    -DEXPECTED_EXIT_CODE=2
    -DEXECUTABLE=${GRACE_EXE}
    -DEMULATOR=${GRACE_EMULATOR}
    "-DARGS=--headless;--frames"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exit_code.cmake)

# OpenGL backend tests. They need an X server, so they are wrapped in
# xvfb-run -a when available, labelled "opengl", and exit with code 77 when no
# GL context can be created (reported as Skipped via SKIP_RETURN_CODE).
if(GRACE_ENABLE_OPENGL)
    add_executable(test_opengl_context test_opengl_context.cpp)
    target_link_libraries(test_opengl_context PRIVATE grace_render_opengl grace_warnings)
    target_include_directories(test_opengl_context PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_opengl_render test_opengl_render.cpp)
    target_link_libraries(test_opengl_render PRIVATE
        grace_null_render grace_render_opengl grace_warnings)
    target_include_directories(test_opengl_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # Volume light injection (milestone 7, third increment). Registered with the
    # OpenGL tests because its numeric half reads back what the injection shader
    # wrote, and only a real GL context executes a shader: the null backend
    # records events and does no arithmetic. It links the null backend too,
    # because its structural half (off by default, release and recreate,
    # rejected configuration) is proved there. SKIP_RETURN_CODE 77 keeps a
    # machine without GL from failing the suite, the rule every OpenGL test here
    # follows.
    add_executable(test_volume_inject test_volume_inject.cpp)
    target_link_libraries(test_volume_inject PRIVATE
        grace_null_render grace_render_opengl grace_warnings)
    target_include_directories(test_volume_inject PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # Volume light propagation (milestone 7, fourth increment). Registered with
    # the OpenGL tests for the same reason as the injection suite: its numeric
    # half compares volume readback against the average of six neighbours, and
    # only a real GL context executes a shader. The structural half (off by
    # default, the pointer swap, volume B released with the volume) is proved
    # on the null backend, which the suite also links.
    add_executable(test_volume_propagate test_volume_propagate.cpp)
    target_link_libraries(test_volume_propagate PRIVATE
        grace_null_render grace_render_opengl grace_warnings)
    target_include_directories(test_volume_propagate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # Volume light compose (milestone 7, fourth increment). Registered with the
    # OpenGL tests for the same reason as the injection and propagation suites:
    # its numeric half reads back pixels the compose shader wrote, and only a
    # real GL context executes a shader. The structural half (the frame order,
    # the submit record, the rejected configuration) is proved on the null
    # backend, which the suite also links.
    add_executable(test_volume_compose test_volume_compose.cpp)
    target_link_libraries(test_volume_compose PRIVATE
        grace_null_render grace_render_opengl grace_warnings)
    target_include_directories(test_volume_compose PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_mesh test_mesh.cpp)
    target_link_libraries(test_mesh PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_mesh PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_shader test_shader.cpp)
    target_link_libraries(test_shader PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_shader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_lighting test_lighting.cpp)
    target_link_libraries(test_lighting PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_lighting PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_shadows test_shadows.cpp)
    target_link_libraries(test_shadows PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_shadows PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_point_shadows test_point_shadows.cpp)
    target_link_libraries(test_point_shadows PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_point_shadows PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_tsm test_tsm.cpp)
    target_link_libraries(test_tsm PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_tsm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_deferred test_deferred.cpp)
    target_link_libraries(test_deferred PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_deferred PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_post test_post.cpp)
    target_link_libraries(test_post PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_post PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_ssao test_ssao.cpp)
    target_link_libraries(test_ssao PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_ssao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_dof_mlaa test_dof_mlaa.cpp)
    target_link_libraries(test_dof_mlaa PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_dof_mlaa PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_volumetric test_volumetric.cpp)
    target_link_libraries(test_volumetric PRIVATE grace_render grace_render_opengl grace_warnings)
    target_include_directories(test_volumetric PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    find_program(GRACE_XVFB_RUN NAMES xvfb-run)
    if(GRACE_XVFB_RUN)
        set(GRACE_OPENGL_LAUNCHER "${GRACE_XVFB_RUN};-a")
    else()
        set(GRACE_OPENGL_LAUNCHER "")
    endif()

    foreach(test_name test_opengl_context test_opengl_render test_volume_inject test_volume_propagate test_volume_compose test_mesh test_shader test_lighting test_shadows test_point_shadows test_tsm test_deferred test_post test_ssao test_dof_mlaa test_volumetric)
        add_test(NAME ${test_name}
            COMMAND ${GRACE_OPENGL_LAUNCHER} $<TARGET_FILE:${test_name}>)
        set_tests_properties(${test_name} PROPERTIES
            LABELS "opengl"
            SKIP_RETURN_CODE 77)
    endforeach()
endif()

# Vulkan backend tests. The same shape as the OpenGL block: an X server for
# the window-system side (xvfb-run -a when available), the "vulkan" label,
# and exit code 77 when no Vulkan device can be created (SKIP_RETURN_CODE,
# reported as Skipped; lavapipe satisfies it on machines without a GPU).
if(GRACE_ENABLE_VULKAN)
    add_executable(test_vulkan_render test_vulkan_render.cpp)
    target_link_libraries(test_vulkan_render PRIVATE
        grace_null_render grace_render_vulkan grace_warnings)
    target_include_directories(test_vulkan_render PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    find_program(GRACE_XVFB_RUN_VK NAMES xvfb-run)
    if(GRACE_XVFB_RUN_VK)
        set(GRACE_VULKAN_LAUNCHER "${GRACE_XVFB_RUN_VK};-a")
    else()
        set(GRACE_VULKAN_LAUNCHER "")
    endif()
    add_test(NAME test_vulkan_render
        COMMAND ${GRACE_VULKAN_LAUNCHER} $<TARGET_FILE:test_vulkan_render>)
    set_tests_properties(test_vulkan_render PROPERTIES
        LABELS "vulkan"
        SKIP_RETURN_CODE 77)
endif()

# Metal backend tests. The mapping checks are pure: they run on every
# platform with no device (Linux included — that is milestone 20's
# recorded validation scope). The device-dependent contract block needs
# the Metal framework and a default device, so it skips with exit code 77
# everywhere the framework is absent (always, on Linux); the "metal"
# label and the SKIP_RETURN_CODE follow the Vulkan block's shape.
if(GRACE_ENABLE_METAL)
    add_executable(test_metal_render test_metal_render.cpp)
    target_link_libraries(test_metal_render PRIVATE
        grace_null_render grace_render_metal grace_warnings)
    target_include_directories(test_metal_render PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(test_metal_render PRIVATE
        ${CMAKE_SOURCE_DIR}/src/render/metal)
    add_test(NAME test_metal_render
        COMMAND $<TARGET_FILE:test_metal_render>)
    set_tests_properties(test_metal_render PROPERTIES
        LABELS "metal"
        SKIP_RETURN_CODE 77)
endif()
