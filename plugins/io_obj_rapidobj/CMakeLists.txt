option(QMESH_PLUGIN_OBJ_RAPIDOBJ "Build rapidobj OBJ input plugin" ON)
option(QMESH_PLUGIN_OBJ_RAPIDOBJ_FETCH_RAPIDOBJ "Fetch rapidobj from GitHub when not installed" OFF)

if (NOT QMESH_PLUGIN_OBJ_RAPIDOBJ)
    message(STATUS "Skipping io_obj_rapidobj - plugin disabled")
    return()
endif()

set(_rapidobj_hints
    ${CMAKE_SOURCE_DIR}/third_party/rapidobj/include
    ${CMAKE_SOURCE_DIR}/third_party/rapidobj
    ${CMAKE_SOURCE_DIR}/external/rapidobj/include
    ${CMAKE_SOURCE_DIR}/external/rapidobj
    ${CMAKE_SOURCE_DIR}/../../meshlab/src/external/downloads/rapidobj/include
    ${CMAKE_SOURCE_DIR}/../../meshlab/src/external/downloads/rapidobj
    $ENV{HOME}/Documents/devel/meshlab/src/external/downloads/rapidobj/include
    $ENV{HOME}/Documents/devel/meshlab/src/external/downloads/rapidobj
)

set(_rapidobj_target "")
set(RAPIDOBJ_INCLUDE_DIR "")

find_package(RapidObj CONFIG QUIET)
if (TARGET rapidobj::rapidobj)
    set(_rapidobj_target rapidobj::rapidobj)
endif()

if (NOT _rapidobj_target)
    find_path(RAPIDOBJ_INCLUDE_DIR rapidobj/rapidobj.hpp
        PATHS ${_rapidobj_hints}
    )
endif()

if (NOT _rapidobj_target AND NOT RAPIDOBJ_INCLUDE_DIR AND QMESH_PLUGIN_OBJ_RAPIDOBJ_FETCH_RAPIDOBJ)
    include(FetchContent)
    FetchContent_Declare(
        rapidobj
        GIT_REPOSITORY https://github.com/guybrush77/rapidobj.git
        GIT_TAG v1.1
    )
    FetchContent_MakeAvailable(rapidobj)
    if (TARGET rapidobj::rapidobj)
        set(_rapidobj_target rapidobj::rapidobj)
    endif()
endif()

if (NOT _rapidobj_target AND NOT RAPIDOBJ_INCLUDE_DIR)
    message(STATUS "Skipping io_obj_rapidobj - missing rapidobj (set RAPIDOBJ_INCLUDE_DIR or enable fetch)")
    return()
endif()

add_library(QMeshLabPluginIORapidObj STATIC
    rapidobjimportplugin.h
    rapidobjimportplugin.cpp
)

target_include_directories(QMeshLabPluginIORapidObj
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vcglib
        ${CMAKE_SOURCE_DIR}/vcglib/eigenlib
)

if (RAPIDOBJ_INCLUDE_DIR)
    target_include_directories(QMeshLabPluginIORapidObj PUBLIC ${RAPIDOBJ_INCLUDE_DIR})
endif()

target_link_libraries(QMeshLabPluginIORapidObj
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

if (_rapidobj_target)
    target_link_libraries(QMeshLabPluginIORapidObj PUBLIC ${_rapidobj_target})
else()
    find_package(Threads QUIET)
    if (Threads_FOUND)
        target_link_libraries(QMeshLabPluginIORapidObj PUBLIC Threads::Threads)
    endif()
endif()
