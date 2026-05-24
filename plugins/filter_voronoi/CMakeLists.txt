option(QMESH_PLUGIN_FILTER_VORONOI "Build Voronoi sampling filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_VORONOI)
    message(STATUS "Skipping filter_voronoi - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterVoronoi STATIC
    voronoifilterplugin.h
    voronoifilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterVoronoi
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterVoronoi
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterVoronoi "filter_voronoi_descriptors"
    PREFIX "/filters/qmeshlab.filter.voronoi"
    FILES filters.json
)
