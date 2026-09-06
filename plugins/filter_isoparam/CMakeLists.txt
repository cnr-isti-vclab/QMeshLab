option(QMESH_PLUGIN_FILTER_ISOPARAM "Build the isoparametrization filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_ISOPARAM)
    message(STATUS "Skipping filter_isoparam - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterIsoParam STATIC
    atlaslayout.h
    isoparamfilterplugin.h
    isoparamfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterIsoParam
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        # The vendored headers include their siblings with <angled> includes.
        ${CMAKE_CURRENT_SOURCE_DIR}/upstream
)

target_link_libraries(QMeshLabPluginFilterIsoParam
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterIsoParam "filter_isoparam_descriptors"
    PREFIX "/filters/qmeshlab.filter.isoparam"
    FILES filters.json
)
