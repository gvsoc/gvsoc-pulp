install(DIRECTORY . DESTINATION ${CMAKE_INSTALL_PREFIX}/generators
FILES_MATCHING PATTERN "*.py" PATTERN "*.json" PATTERN __pycache__ EXCLUDE PATTERN test EXCLUDE)

add_subdirectory(pulp)
