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

target_link_libraries(test_application PRIVATE grace_core grace_null_render grace_warnings)
target_include_directories(test_application PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME application COMMAND test_application)

# CLI smoke tests for grace_app: a successful headless run must exit 0, an
# unknown option must exit non-zero (usage error, exit code 2).
add_test(NAME app_headless_run COMMAND grace_app
    --headless --frames 4 --frame-time-ns 20000000 --step-ns 10000000
    --width 320 --height 240)

add_test(NAME app_usage_error COMMAND grace_app --no-such-option)
set_tests_properties(app_usage_error PROPERTIES WILL_FAIL TRUE)
