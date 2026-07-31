option(QMESH_PLUGIN_FILTER_VERTEX_DISPLACEMENT "Build vertex displacement filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_VERTEX_DISPLACEMENT)
    message(STATUS "Skipping filter_vertex_displacement - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterVertexDisplacement STATIC
    vertexdisplacementfilterplugin.h
    vertexdisplacementfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterVertexDisplacement
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterVertexDisplacement
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterVertexDisplacement "filter_vertex_displacement_descriptors"
    PREFIX "/filters/qmeshlab.filter.vertex_displacement"
    FILES filters.json
)
