option(QMESH_PLUGIN_FILTER_EMBREE "Build Embree-based mesh filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_EMBREE)
    message(STATUS "Skipping filter_embree - plugin disabled")
    return()
endif()

# Embree + OpenMP detection is shared with QMeshLabCore (see cmake/QMeshLabEmbree.cmake).
if (NOT QMESHLAB_EMBREE_FOUND)
    message(STATUS "Skipping filter_embree - embree (v4) / OpenMP not available")
    return()
endif()

add_library(QMeshLabPluginFilterEmbree STATIC
    embreefilterplugin.h
    embreefilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterEmbree
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterEmbree
    PUBLIC
        QMeshLabCore
        Qt6::Core
        QMeshLabEmbree
)

qt_add_resources(QMeshLabPluginFilterEmbree "filter_embree_descriptors"
    PREFIX "/filters/qmeshlab.filter.embree"
    FILES filters.json
)
