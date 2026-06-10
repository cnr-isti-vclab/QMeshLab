option(QMESH_PLUGIN_FILTER_CAMERA "Build camera filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_CAMERA)
    message(STATUS "Skipping filter_camera - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterCamera STATIC
    camerafilterplugin.h
    camerafilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterCamera
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterCamera
    PUBLIC
        QMeshLabCore
        Qt6::Core
        Qt6::Gui
)

qt_add_resources(QMeshLabPluginFilterCamera "filter_camera_descriptors"
    PREFIX "/filters/qmeshlab.filter.camera"
    FILES filters.json
)
