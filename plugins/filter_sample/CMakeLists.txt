option(QMESH_PLUGIN_FILTER_SAMPLE "Build sample filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_SAMPLE)
    message(STATUS "Skipping filter_sample - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterSample STATIC
    samplefilterplugin.h
    samplefilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterSample
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterSample
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterSample "filter_sample_descriptors"
    PREFIX "/filters/qmeshlab.filter.sample"
    FILES filters.json
)
