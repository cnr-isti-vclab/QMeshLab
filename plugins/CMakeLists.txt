add_library(QMeshLabPlugins STATIC
    meshpluginregistry.h
    meshpluginregistry.cpp
    filterpluginregistry.h
    filterpluginregistry.cpp
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

add_subdirectory(io_obj_rapidobj)
if (TARGET QMeshLabPluginIORapidObj)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginIORapidObj)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_IO_RAPIDOBJ_ENABLED=1)
endif()

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

add_subdirectory(io_gltf)
if (TARGET QMeshLabPluginIOGLTF)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginIOGLTF)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_IO_GLTF_ENABLED=1)
endif()

add_subdirectory(filter_basic)
if (TARGET QMeshLabPluginFilterBasic)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterBasic)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_BASIC_ENABLED=1)
endif()

add_subdirectory(filter_func)
if (TARGET QMeshLabPluginFilterFunc)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterFunc)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_FUNC_ENABLED=1)
endif()

add_subdirectory(filter_embree)
if (TARGET QMeshLabPluginFilterEmbree)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterEmbree)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_EMBREE_ENABLED=1)
endif()

add_subdirectory(filter_select)
if (TARGET QMeshLabPluginFilterSelect)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterSelect)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_SELECT_ENABLED=1)
endif()

add_subdirectory(filter_clean)
if (TARGET QMeshLabPluginFilterClean)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterClean)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_CLEAN_ENABLED=1)
endif()

add_subdirectory(filter_meshing)
if (TARGET QMeshLabPluginFilterMeshing)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterMeshing)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_MESHING_ENABLED=1)
endif()

add_subdirectory(filter_screened_poisson)
if (TARGET QMeshLabPluginFilterScreenedPoisson)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterScreenedPoisson)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_SCREENED_POISSON_ENABLED=1)
endif()
