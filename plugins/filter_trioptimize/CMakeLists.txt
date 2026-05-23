option(QMESH_PLUGIN_FILTER_TRIOPTIMIZE "Build TriOptimize mesh filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_TRIOPTIMIZE)
    message(STATUS "Skipping filter_trioptimize - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterTriOptimize STATIC
    curvdata.h
    curvedgeflip.h
    trioptimizefilterplugin.h
    trioptimizefilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterTriOptimize
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterTriOptimize
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterTriOptimize "filter_trioptimize_descriptors"
    PREFIX "/filters/qmeshlab.filter.trioptimize"
    FILES filters.json
)
