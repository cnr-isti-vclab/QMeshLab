option(QMESH_PLUGIN_FILTER_MEASURE "Build measure filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_MEASURE)
    message(STATUS "Skipping filter_measure - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterMeasure STATIC
    measurefilterplugin.h
    measurefilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterMeasure
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterMeasure
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterMeasure "filter_measure_descriptors"
    PREFIX "/filters/qmeshlab.filter.measure"
    FILES filters.json
)
