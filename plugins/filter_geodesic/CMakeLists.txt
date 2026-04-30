option(QMESH_PLUGIN_FILTER_GEODESIC "Build geodesic filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_GEODESIC)
    message(STATUS "Skipping filter_geodesic - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterGeodesic STATIC
    geodesicfilterplugin.h
    geodesicfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterGeodesic
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterGeodesic
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterGeodesic "filter_geodesic_descriptors"
    PREFIX "/filters/qmeshlab.filter.geodesic"
    FILES filters.json
)
