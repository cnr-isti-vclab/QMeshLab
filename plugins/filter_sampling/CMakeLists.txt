option(QMESH_PLUGIN_FILTER_SAMPLING "Build sampling filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_SAMPLING)
    message(STATUS "Skipping filter_sampling - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterSampling STATIC
    samplingfilterplugin.h
    samplingfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterSampling
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterSampling
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterSampling "filter_sampling_descriptors"
    PREFIX "/filters/qmeshlab.filter.sampling"
    FILES filters.json
)
