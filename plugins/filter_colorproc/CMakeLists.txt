option(QMESH_PLUGIN_FILTER_COLORPROC "Build colorproc filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_COLORPROC)
    message(STATUS "Skipping filter_colorproc - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterColorProc STATIC
    colorprocfilterplugin.h
    colorprocfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterColorProc
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterColorProc
    PUBLIC
        QMeshLabCore
        Qt6::Core
        Qt6::Gui
)

qt_add_resources(QMeshLabPluginFilterColorProc "filter_colorproc_descriptors"
    PREFIX "/filters/qmeshlab.filter.colorproc"
    FILES filters.json
)
