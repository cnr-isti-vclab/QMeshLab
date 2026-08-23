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

add_subdirectory(io_3mf)
if (TARGET QMeshLabPluginIO3MF)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginIO3MF)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_IO_3MF_ENABLED=1)
endif()

add_subdirectory(io_trueform)
if (TARGET QMeshLabPluginIOTrueForm)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginIOTrueForm)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_IO_TRUEFORM_ENABLED=1)
endif()

add_subdirectory(filter_trueform)
if (TARGET QMeshLabPluginFilterTrueForm)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterTrueForm)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_TRUEFORM_ENABLED=1)
endif()

add_subdirectory(filter_basic)
if (TARGET QMeshLabPluginFilterBasic)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterBasic)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_BASIC_ENABLED=1)
endif()

add_subdirectory(filter_expression)
if (TARGET QMeshLabPluginFilterExpression)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterExpression)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_EXPRESSION_ENABLED=1)
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

add_subdirectory(filter_meshfix)
if (TARGET QMeshLabPluginFilterMeshFix)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterMeshFix)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_MESHFIX_ENABLED=1)
endif()

add_subdirectory(filter_qslim)
if (TARGET QMeshLabPluginFilterQSlim)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterQSlim)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_QSLIM_ENABLED=1)
endif()

add_subdirectory(filter_instant_meshes)
if (TARGET QMeshLabPluginFilterInstantMeshes)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterInstantMeshes)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_INSTANT_MESHES_ENABLED=1)
endif()

add_subdirectory(filter_quadwild)
if (TARGET QMeshLabPluginFilterQuadWild)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterQuadWild)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_QUADWILD_ENABLED=1)
endif()

add_subdirectory(filter_meshing)
if (TARGET QMeshLabPluginFilterMeshing)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterMeshing)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_MESHING_ENABLED=1)
endif()

add_subdirectory(filter_cgal)
if (TARGET QMeshLabPluginFilterCGAL)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterCGAL)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_CGAL_ENABLED=1)
endif()

add_subdirectory(filter_igl)
if (TARGET QMeshLabPluginFilterIgl)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterIgl)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_IGL_ENABLED=1)
endif()

add_subdirectory(filter_screened_poisson)
if (TARGET QMeshLabPluginFilterScreenedPoisson)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterScreenedPoisson)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_SCREENED_POISSON_ENABLED=1)
endif()

add_subdirectory(filter_sampling)
if (TARGET QMeshLabPluginFilterSampling)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterSampling)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_SAMPLING_ENABLED=1)
endif()

add_subdirectory(filter_voronoi)
if (TARGET QMeshLabPluginFilterVoronoi)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterVoronoi)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_VORONOI_ENABLED=1)
endif()

add_subdirectory(filter_icp)
if (TARGET QMeshLabPluginFilterICP)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterICP)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_ICP_ENABLED=1)
endif()

add_subdirectory(filter_unsharp)
if (TARGET QMeshLabPluginFilterUnsharp)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterUnsharp)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_UNSHARP_ENABLED=1)
endif()

add_subdirectory(filter_create)
if (TARGET QMeshLabPluginFilterCreate)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterCreate)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_CREATE_ENABLED=1)
endif()

add_subdirectory(filter_geodesic)
if (TARGET QMeshLabPluginFilterGeodesic)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterGeodesic)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_GEODESIC_ENABLED=1)
endif()

add_subdirectory(filter_texture)
if (TARGET QMeshLabPluginFilterTexture)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterTexture)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_TEXTURE_ENABLED=1)
endif()

add_subdirectory(filter_texture_defragmentation)
if (TARGET QMeshLabPluginFilterTextureDefragmentation)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterTextureDefragmentation)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_TEXTURE_DEFRAGMENTATION_ENABLED=1)
endif()

add_subdirectory(filter_measure)
if (TARGET QMeshLabPluginFilterMeasure)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterMeasure)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_MEASURE_ENABLED=1)
endif()

add_subdirectory(filter_mls)
if (TARGET QMeshLabPluginFilterMLS)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterMLS)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_MLS_ENABLED=1)
endif()

add_subdirectory(filter_vertex_displacement)
if (TARGET QMeshLabPluginFilterVertexDisplacement)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterVertexDisplacement)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_VERTEX_DISPLACEMENT_ENABLED=1)
endif()

add_subdirectory(filter_layer)
if (TARGET QMeshLabPluginFilterLayer)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterLayer)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_LAYER_ENABLED=1)
endif()

add_subdirectory(filter_colorproc)
if (TARGET QMeshLabPluginFilterColorProc)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterColorProc)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_COLORPROC_ENABLED=1)
endif()

add_subdirectory(filter_xatlas)
if (TARGET QMeshLabPluginFilterXAtlas)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterXAtlas)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_XATLAS_ENABLED=1)
endif()

add_subdirectory(filter_trioptimize)
if (TARGET QMeshLabPluginFilterTriOptimize)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterTriOptimize)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_TRIOPTIMIZE_ENABLED=1)
endif()

add_subdirectory(filter_color_projection)
if (TARGET QMeshLabPluginFilterColorProjection)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterColorProjection)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_COLOR_PROJECTION_ENABLED=1)
endif()

add_subdirectory(filter_camera)
if (TARGET QMeshLabPluginFilterCamera)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterCamera)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_CAMERA_ENABLED=1)
endif()

add_subdirectory(filter_img_patch_param)
if (TARGET QMeshLabPluginFilterImgPatchParam)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterImgPatchParam)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_IMG_PATCH_PARAM_ENABLED=1)
endif()

add_subdirectory(filter_plymc)
if (TARGET QMeshLabPluginFilterPlyMC)
    target_link_libraries(QMeshLabPlugins PUBLIC QMeshLabPluginFilterPlyMC)
    target_compile_definitions(QMeshLabPlugins PRIVATE QMESH_PLUGIN_FILTER_PLYMC_ENABLED=1)
endif()
