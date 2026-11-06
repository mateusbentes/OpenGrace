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

add_executable(test_lights test_lights.cpp)

target_link_libraries(test_lights PRIVATE grace_render grace_null_render grace_warnings)
target_include_directories(test_lights PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME lights COMMAND test_lights)

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
target_include_directories(test_shader_source_rule PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME shader_source_rule COMMAND test_shader_source_rule ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(test_renderer test_renderer.cpp)

target_link_libraries(test_renderer PRIVATE grace_render grace_null_render grace_warnings)
target_include_directories(test_renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME renderer COMMAND test_renderer)

# CLI tests for the grace executable, checked for their exact exit code
# through check_exit_code.cmake in script mode (no test framework):
# 0 = success, 2 = usage error.
set(GRACE_EXE $<TARGET_FILE:grace>)

add_test(NAME app_headless_run COMMAND ${CMAKE_COMMAND}
    -DEXPECTED_EXIT_CODE=0
    -DEXECUTABLE=${GRACE_EXE}
    "-DARGS=--headless;--frames;4;--frame-time-ns;20000000;--step-ns;10000000;--width;320;--height;240"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exit_code.cmake)

add_test(NAME app_usage_error_unknown_option COMMAND ${CMAKE_COMMAND}
    -DEXPECTED_EXIT_CODE=2
    -DEXECUTABLE=${GRACE_EXE}
    "-DARGS=--no-such-option"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exit_code.cmake)

add_test(NAME app_usage_error_missing_value COMMAND ${CMAKE_COMMAND}
    -DEXPECTED_EXIT_CODE=2
    -DEXECUTABLE=${GRACE_EXE}
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

    find_program(GRACE_XVFB_RUN NAMES xvfb-run)
    if(GRACE_XVFB_RUN)
        set(GRACE_OPENGL_LAUNCHER "${GRACE_XVFB_RUN};-a")
    else()
        set(GRACE_OPENGL_LAUNCHER "")
    endif()

    foreach(test_name test_opengl_context test_opengl_render test_mesh test_shader test_lighting test_shadows test_point_shadows test_tsm test_deferred test_post)
        add_test(NAME ${test_name}
            COMMAND ${GRACE_OPENGL_LAUNCHER} $<TARGET_FILE:${test_name}>)
        set_tests_properties(${test_name} PROPERTIES
            LABELS "opengl"
            SKIP_RETURN_CODE 77)
    endforeach()
endif()
