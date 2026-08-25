# This directory is registered as a GVSOC_MODULES root (see the top-level
# Makefile) so the modules under it (e.g. ips/pulp/idma_v2/...) appear
# under the corresponding dotted Python module paths (ips.pulp.idma_v2,
# ...). Mirror that by installing the directory's contents flat into
# install/generators/, so the runtime sys.path entry resolves the same
# packages it does at source-tree time.
install(DIRECTORY . DESTINATION ${CMAKE_INSTALL_PREFIX}/generators
FILES_MATCHING PATTERN "*.py" PATTERN "*.json" PATTERN __pycache__ EXCLUDE)
