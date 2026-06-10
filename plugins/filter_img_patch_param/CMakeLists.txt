option(QMESH_PLUGIN_FILTER_IMG_PATCH_PARAM "Build image patch parametrization filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_IMG_PATCH_PARAM)
    message(STATUS "Skipping filter_img_patch_param - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterImgPatchParam STATIC
    imgpatchparamfilterplugin.h
    imgpatchparamfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterImgPatchParam
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterImgPatchParam
    PUBLIC
        QMeshLabCore
        Qt6::Core
        Qt6::Gui
)

qt_add_resources(QMeshLabPluginFilterImgPatchParam "filter_img_patch_param_descriptors"
    PREFIX "/filters/qmeshlab.filter.img_patch_param"
    FILES filters.json
)
