option(QMESH_PLUGIN_FILTER_PLYMC "Build PLYMC filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_PLYMC)
    message(STATUS "Skipping filter_plymc - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterPlyMC STATIC
    plymcfilterplugin.h
    plymcfilterplugin.cpp
    ${CMAKE_SOURCE_DIR}/vcglib/wrap/ply/plylib.cpp
)

target_include_directories(QMeshLabPluginFilterPlyMC
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vcglib
)

target_link_libraries(QMeshLabPluginFilterPlyMC
    PUBLIC
        QMeshLabCore
        Qt6::Core
        Qt6::Gui
)

qt_add_resources(QMeshLabPluginFilterPlyMC "filter_plymc_descriptors"
    PREFIX "/filters/qmeshlab.filter.plymc"
    FILES filters.json
)
