option(QMESH_PLUGIN_FILTER_ICP "Build ICP alignment filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_ICP)
    message(STATUS "Skipping filter_icp - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterICP STATIC
    icpfilterplugin.h
    icpfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterICP
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterICP
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterICP "filter_icp_descriptors"
    PREFIX "/filters/qmeshlab.filter.icp"
    FILES filters.json
)
