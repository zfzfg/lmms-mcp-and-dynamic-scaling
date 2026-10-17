find_program(MCP_NODE_EXECUTABLE NAMES node)
find_program(MCP_NPM_EXECUTABLE NAMES npm.cmd npm)
if(NOT MCP_NODE_EXECUTABLE OR NOT MCP_NPM_EXECUTABLE)
 message(FATAL_ERROR "MCP development builds require Node.js 24.19.0 and npm; installed packages bundle their own runtime")
endif()
set(MCP_PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mcp-package")
file(GLOB_RECURSE MCP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ts" "${CMAKE_CURRENT_SOURCE_DIR}/test/*.ts")
add_custom_command(OUTPUT "${MCP_PACKAGE_DIR}/versions.json"
 COMMAND "${MCP_NPM_EXECUTABLE}" ci --ignore-scripts --no-audit --no-fund
 COMMAND "${MCP_NPM_EXECUTABLE}" run build
 COMMAND "${MCP_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package.mjs" --out "${MCP_PACKAGE_DIR}"
 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
 DEPENDS ${MCP_SOURCES} package.json package-lock.json scripts/package.mjs
 VERBATIM)
add_custom_target(mcp-companion ALL DEPENDS "${MCP_PACKAGE_DIR}/versions.json")
add_executable(lmms-mcp packaging/launcher.cpp)
target_compile_features(lmms-mcp PRIVATE cxx_std_20)
if(MINGW)
 target_link_options(lmms-mcp PRIVATE -municode)
endif()
file(RELATIVE_PATH MCP_PACKAGE_RELATIVE "/${BIN_DIR}" "/${LIB_DIR}/lmms/mcp")
target_compile_definitions(lmms-mcp PRIVATE "LMMS_MCP_PACKAGE_RELATIVE=\"${MCP_PACKAGE_RELATIVE}\"")
add_dependencies(lmms-mcp mcp-companion)
install(TARGETS lmms-mcp RUNTIME DESTINATION "${BIN_DIR}")
install(DIRECTORY "${MCP_PACKAGE_DIR}/" DESTINATION "${LIB_DIR}/lmms/mcp" PATTERN ".lmms-mcp-package" EXCLUDE)
