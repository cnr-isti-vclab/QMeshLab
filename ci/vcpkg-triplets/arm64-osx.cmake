# Overlay triplet that shadows vcpkg's builtin arm64-osx.
#
# Identical to the builtin apart from VCPKG_OSX_DEPLOYMENT_TARGET. Without that,
# dependencies are compiled against the build machine's SDK default, so their
# objects carry a newer minimum macOS than the application's own
# CMAKE_OSX_DEPLOYMENT_TARGET. The linker only warns about the mismatch, and the
# resulting bundle then advertises LSMinimumSystemVersion 15.0 while containing
# code that may not run there — the app launches on macOS 15 and dies with a dyld
# error instead of being refused politely.
#
# Keep this value in sync with CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt.
# It cannot be derived from there: vcpkg evaluates triplets in a separate process.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 15.0)
