option(QMESH_PLUGIN_FILTER_MESHING "Build meshing filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_MESHING)
    message(STATUS "Skipping filter_meshing - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterMeshing STATIC
    meshingfilterplugin.h
    meshingfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterMeshing
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterMeshing
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

