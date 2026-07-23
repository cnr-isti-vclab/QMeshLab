option(QMESH_PLUGIN_FILTER_TEXTURE "Build texture filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_TEXTURE)
    message(STATUS "Skipping filter_texture - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterTexture STATIC
    texturefilterplugin.h
    texturefilterplugin.cpp
    rastering.h
    pushpull.h
    texture_packer.hpp
    texture_packer.cpp
)

target_include_directories(QMeshLabPluginFilterTexture
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterTexture
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterTexture "filter_texture_descriptors"
    PREFIX "/filters/qmeshlab.filter.texture"
    FILES filters.json
)
