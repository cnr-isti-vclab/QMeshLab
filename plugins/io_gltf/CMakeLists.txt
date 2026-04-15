option(QMESH_PLUGIN_GLTF "Build glTF/glb input plugin" ON)
option(QMESH_PLUGIN_GLTF_FETCH_TINYGLTF "Fetch tinygltf from GitHub when not installed" OFF)

if (NOT QMESH_PLUGIN_GLTF)
    message(STATUS "Skipping io_gltf - plugin disabled")
    return()
endif()

set(_tinygltf_hints
    ${CMAKE_SOURCE_DIR}/third_party/tinygltf
    ${CMAKE_SOURCE_DIR}/external/tinygltf
    ${CMAKE_SOURCE_DIR}/../../meshlab/src/external/downloads/tinygltf-2.6.3
    $ENV{HOME}/Documents/devel/meshlab/src/external/downloads/tinygltf-2.6.3
)

find_path(TINYGLTF_INCLUDE_DIR tiny_gltf.h
    PATHS ${_tinygltf_hints}
)

if (NOT TINYGLTF_INCLUDE_DIR)
    find_path(TINYGLTF_INCLUDE_DIR tiny_gltf.h)
endif()

if (NOT TINYGLTF_INCLUDE_DIR AND QMESH_PLUGIN_GLTF_FETCH_TINYGLTF)
    include(FetchContent)
    FetchContent_Declare(
        tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG v2.8.21
    )
    FetchContent_MakeAvailable(tinygltf)
    if (tinygltf_SOURCE_DIR)
        set(TINYGLTF_INCLUDE_DIR ${tinygltf_SOURCE_DIR})
    endif()
endif()

if (NOT TINYGLTF_INCLUDE_DIR)
    message(STATUS "Skipping io_gltf - missing tinygltf (set TINYGLTF_INCLUDE_DIR or enable fetch)")
    return()
endif()

add_library(QMeshLabPluginIOGLTF STATIC
    gltfimportplugin.h
    gltfimportplugin.cpp
)

target_include_directories(QMeshLabPluginIOGLTF
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vcglib
        ${CMAKE_SOURCE_DIR}/vcglib/eigenlib
        ${TINYGLTF_INCLUDE_DIR}
)

target_link_libraries(QMeshLabPluginIOGLTF
    PUBLIC
        QMeshLabCore
        Qt6::Core
        Qt6::Gui
)
