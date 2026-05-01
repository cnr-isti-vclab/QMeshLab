option(QMESH_PLUGIN_FILTER_UNSHARP "Build unsharp filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_UNSHARP)
    message(STATUS "Skipping filter_unsharp - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterUnsharp STATIC
    unsharpfilterplugin.h
    unsharpfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterUnsharp
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterUnsharp
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterUnsharp "filter_unsharp_descriptors"
    PREFIX "/filters/qmeshlab.filter.unsharp"
    FILES filters.json
)
