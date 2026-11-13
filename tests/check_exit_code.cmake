# Exact exit-code checker, run in script mode (no test framework):
#
#   cmake -DEXPECTED_EXIT_CODE=<n> -DEXECUTABLE=<path> "-DARGS=<cmake-list>" \
#         -P check_exit_code.cmake
#
# ARGS is a semicolon-separated CMake list of program arguments. The script
# fails with a clear message unless the program exits with exactly the
# expected code.

if(NOT DEFINED EXPECTED_EXIT_CODE)
    message(FATAL_ERROR "check_exit_code: EXPECTED_EXIT_CODE is required")
endif()

if(NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "check_exit_code: EXECUTABLE is required")
endif()

if(NOT DEFINED ARGS)
    set(ARGS "")
endif()

if(NOT DEFINED EMULATOR)
    set(EMULATOR "")
endif()

if(EMULATOR)
    execute_process(
        COMMAND ${EMULATOR} "${EXECUTABLE}" ${ARGS}
        RESULT_VARIABLE actual_code
        OUTPUT_VARIABLE stdout_output
        ERROR_VARIABLE stderr_output)
else()
    execute_process(
        COMMAND "${EXECUTABLE}" ${ARGS}
        RESULT_VARIABLE actual_code
        OUTPUT_VARIABLE stdout_output
        ERROR_VARIABLE stderr_output)
endif()

if(NOT "${actual_code}" STREQUAL "${EXPECTED_EXIT_CODE}")
    message(FATAL_ERROR
        "check_exit_code: expected exit code ${EXPECTED_EXIT_CODE}, "
        "got '${actual_code}' for: ${EXECUTABLE} ${ARGS}\n"
        "stdout:\n${stdout_output}\n"
        "stderr:\n${stderr_output}")
endif()

message(STATUS
    "check_exit_code: '${EXECUTABLE} ${ARGS}' exited with ${actual_code} as expected")
