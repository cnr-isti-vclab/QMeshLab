option(QMESH_PLUGIN_FILTER_LAYER "Build layer filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_LAYER)
    message(STATUS "Skipping filter_layer - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterLayer STATIC
    layerfilterplugin.h
    layerfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterLayer
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterLayer
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterLayer "filter_layer_descriptors"
    PREFIX "/filters/qmeshlab.filter.layer"
    FILES filters.json
)
