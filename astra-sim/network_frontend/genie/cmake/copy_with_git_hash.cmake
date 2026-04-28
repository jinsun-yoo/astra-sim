# Called at build time (via POST_BUILD) to copy the binary with a 6-char git hash suffix.
# Required variables (passed with -D on the cmake -P command line):
#   SOURCE_DIR  - repository root used for `git rev-parse`
#   BINARY      - full path of the just-built binary (from $<TARGET_FILE:...>)
#   OUTPUT_DIR  - destination directory (from $<TARGET_FILE_DIR:...>)

execute_process(
    COMMAND git -C "${SOURCE_DIR}" rev-parse --short=6 HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT
)

if(NOT GIT_RESULT EQUAL 0 OR NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()

set(DEST "${OUTPUT_DIR}/AstraSim_Genie_${GIT_HASH}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BINARY}" "${DEST}"
)
message(STATUS "Git-hashed binary: ${DEST}")
