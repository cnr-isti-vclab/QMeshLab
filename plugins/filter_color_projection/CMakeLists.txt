option(QMESH_PLUGIN_FILTER_COLOR_PROJECTION "Build color projection filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_COLOR_PROJECTION)
    message(STATUS "Skipping filter_color_projection - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterColorProjection STATIC
    colorprojectionfilterplugin.h
    colorprojectionfilterplugin.cpp
    floatbuffer.h
    pushpull.h
)

target_include_directories(QMeshLabPluginFilterColorProjection
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterColorProjection
    PUBLIC
        QMeshLabCore
        Qt6::Core
        Qt6::Gui
)

qt_add_resources(QMeshLabPluginFilterColorProjection "filter_color_projection_descriptors"
    PREFIX "/filters/qmeshlab.filter.color_projection"
    FILES filters.json
)
