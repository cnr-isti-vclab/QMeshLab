option(QMESH_PLUGIN_FILTER_TEXTURE_DEFRAGMENTATION "Build texture defragmentation filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_TEXTURE_DEFRAGMENTATION)
    message(STATUS "Skipping filter_texture_defragmentation - plugin disabled")
    return()
endif()

set(TEXTURE_DEFRAG_UPSTREAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/upstream/TextureDefragmentation/src)

add_library(QMeshLabPluginFilterTextureDefragmentation STATIC
    ../filter_texture/texture_packer.cpp
    ../filter_texture/texture_packer.hpp

    texturedefragfilterplugin.h
    texturedefragfilterplugin.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/arap.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/intersection.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/logging.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/matching.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/mesh.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/mesh_attribute.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/mesh_graph.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/packing.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/seam_remover.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/seams.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/shell.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/texture_object.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/texture_optimization.cpp
    ${TEXTURE_DEFRAG_UPSTREAM_DIR}/texture_rendering.cpp
    ${CMAKE_SOURCE_DIR}/vcglib/wrap/qt/outline2_rasterizer.cpp
)

target_include_directories(QMeshLabPluginFilterTextureDefragmentation
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${TEXTURE_DEFRAG_UPSTREAM_DIR}
)

target_link_libraries(QMeshLabPluginFilterTextureDefragmentation
    PUBLIC
        QMeshLabCore
        Qt6::Core
        Qt6::Gui
)

if(MSVC)
    target_compile_definitions(QMeshLabPluginFilterTextureDefragmentation PRIVATE _USE_MATH_DEFINES)
endif()

qt_add_resources(QMeshLabPluginFilterTextureDefragmentation "filter_texture_defragmentation_descriptors"
    PREFIX "/filters/qmeshlab.filter.texture_defragmentation"
    FILES filters.json
)
