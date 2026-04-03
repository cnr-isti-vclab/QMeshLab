add_library(QMeshLabPlugins STATIC
    meshpluginregistry.h
    meshpluginregistry.cpp
)

target_include_directories(QMeshLabPlugins
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPlugins
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

add_subdirectory(io_vcg)
if (TARGET QMeshLabPluginIOVCG)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginIOVCG)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_IO_VCG_ENABLED=1)
endif()

add_subdirectory(io_e57)
if (TARGET QMeshLabPluginIOE57)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginIOE57)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_IO_E57_ENABLED=1)
endif()
