option(QMESH_PLUGIN_FILTER_MLS "Build MLS filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_MLS)
    message(STATUS "Skipping filter_mls - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterMLS STATIC
    mlsfilterplugin.h
    mlsfilterplugin.cpp
    apss.cpp
    balltree.cpp
    rimls.cpp
)

target_include_directories(QMeshLabPluginFilterMLS
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterMLS
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterMLS "filter_mls_descriptors"
    PREFIX "/filters/qmeshlab.filter.mls"
    FILES filters.json
)
