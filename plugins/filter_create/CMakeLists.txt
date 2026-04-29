option(QMESH_PLUGIN_FILTER_CREATE "Build mesh creation filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_CREATE)
    message(STATUS "Skipping filter_create - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterCreate STATIC
    createfilterplugin.h
    createfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterCreate
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterCreate
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterCreate "filter_create_descriptors"
    PREFIX "/filters/qmeshlab.filter.create"
    FILES filters.json
)
