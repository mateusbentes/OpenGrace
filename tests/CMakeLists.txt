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

    find_program(GRACE_XVFB_RUN NAMES xvfb-run)
    if(GRACE_XVFB_RUN)
        set(GRACE_OPENGL_LAUNCHER "${GRACE_XVFB_RUN};-a")
    else()
        set(GRACE_OPENGL_LAUNCHER "")
    endif()

    foreach(test_name test_opengl_context test_opengl_render)
        add_test(NAME ${test_name}
            COMMAND ${GRACE_OPENGL_LAUNCHER} $<TARGET_FILE:${test_name}>)
        set_tests_properties(${test_name} PROPERTIES
            LABELS "opengl"
            SKIP_RETURN_CODE 77)
    endforeach()
endif()
