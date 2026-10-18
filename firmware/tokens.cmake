# Token generation is offline and uses only Node built-ins, not pnpm packages.
find_program(MOSAIC_NODE_EXECUTABLE NAMES node)
if(NOT MOSAIC_NODE_EXECUTABLE)
    message(FATAL_ERROR "Mosaic token generation requires Node.js on PATH (no pnpm install required).")
endif()

set(MOSAIC_TOKEN_PACKAGE "${CMAKE_CURRENT_LIST_DIR}/../platform/packages/tokens")
file(GLOB_RECURSE MOSAIC_TOKEN_INPUTS CONFIGURE_DEPENDS "${MOSAIC_TOKEN_PACKAGE}/tokens/*.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${MOSAIC_TOKEN_INPUTS}
    "${MOSAIC_TOKEN_PACKAGE}/build.mjs"
    "${CMAKE_CURRENT_LIST_DIR}/main/ui/generated/design_tokens.h")
execute_process(
    COMMAND "${MOSAIC_NODE_EXECUTABLE}" "${MOSAIC_TOKEN_PACKAGE}/build.mjs"
    RESULT_VARIABLE MOSAIC_TOKEN_RESULT
    OUTPUT_VARIABLE MOSAIC_TOKEN_OUTPUT
    ERROR_VARIABLE MOSAIC_TOKEN_ERROR
)
if(NOT MOSAIC_TOKEN_RESULT STREQUAL "0")
    message(FATAL_ERROR "Mosaic token generation failed (${MOSAIC_TOKEN_RESULT}):\n${MOSAIC_TOKEN_OUTPUT}\n${MOSAIC_TOKEN_ERROR}")
endif()
