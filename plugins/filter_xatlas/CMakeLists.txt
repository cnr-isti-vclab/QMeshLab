option(QMESH_PLUGIN_FILTER_XATLAS "Build xatlas parametrization filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_XATLAS)
    message(STATUS "Skipping filter_xatlas - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterXAtlas STATIC
    xatlasfilterplugin.h
    xatlasfilterplugin.cpp
    upstream/xatlas.cpp
)

target_include_directories(QMeshLabPluginFilterXAtlas
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/upstream
)

target_link_libraries(QMeshLabPluginFilterXAtlas
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterXAtlas "filter_xatlas_descriptors"
    PREFIX "/filters/qmeshlab.filter.xatlas"
    FILES filters.json
)
