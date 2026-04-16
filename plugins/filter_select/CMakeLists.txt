option(QMESH_PLUGIN_FILTER_SELECT "Build selection mesh filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_SELECT)
    message(STATUS "Skipping filter_select - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterSelect STATIC
    selectfilterplugin.h
    selectfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterSelect
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterSelect
    PUBLIC
        QMeshLabCore
        Qt6::Core
)
