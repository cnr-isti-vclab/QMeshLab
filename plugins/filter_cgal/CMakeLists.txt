option(QMESH_PLUGIN_FILTER_CGAL "Build CGAL-based mesh filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_CGAL)
    message(STATUS "Skipping filter_cgal - plugin disabled")
    return()
endif()

find_package(CGAL QUIET)

if (NOT TARGET CGAL::CGAL)
    message(STATUS "Skipping filter_cgal - CGAL not found")
    return()
endif()

add_library(QMeshLabPluginFilterCGAL STATIC
    cgalfilterplugin.h
    cgalfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterCGAL
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterCGAL
    PUBLIC
        QMeshLabCore
        Qt6::Core
        CGAL::CGAL
)

qt_add_resources(QMeshLabPluginFilterCGAL "filter_cgal_descriptors"
    PREFIX "/filters/qmeshlab.filter.cgal"
    FILES filters.json
)
