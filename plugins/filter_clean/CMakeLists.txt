option(QMESH_PLUGIN_FILTER_CLEAN "Build cleaning mesh filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_CLEAN)
    message(STATUS "Skipping filter_clean - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterClean STATIC
    cleanfilterplugin.h
    cleanfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterClean
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterClean
    PUBLIC
        QMeshLabCore
        Qt6::Core
)


qt_add_resources(QMeshLabPluginFilterClean "filter_clean_descriptors"
    PREFIX "/filters/qmeshlab.filter.clean"
    FILES filters.json
)
