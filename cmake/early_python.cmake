# This file is included by CMake before processing the top-level CMakeLists.txt
# (via CMAKE_PROJECT_INCLUDE_BEFORE set in CMakePresets.json).
#
# nanobind's cmake config checks for Python::Module at find_package() time, but
# the vcpkg toolchain auto-injects find_package(nanobind) before our main
# CMakeLists.txt gets a chance to call find_package(Python).  Pre-finding Python
# here ensures the Python::Module target exists when nanobind-config.cmake runs.
find_package(Python COMPONENTS Interpreter Development.Embed Development.Module)
