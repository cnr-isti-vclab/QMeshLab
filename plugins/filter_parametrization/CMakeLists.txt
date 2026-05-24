option(QMESH_PLUGIN_FILTER_PARAMETRIZATION "Build libigl parametrization filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_PARAMETRIZATION)
    message(STATUS "Skipping filter_parametrization - plugin disabled")
    return()
endif()

if (NOT TARGET QMeshLabPluginFilterIglCommon)
    message(STATUS "Skipping filter_parametrization - libigl common support not available")
    return()
endif()

add_library(QMeshLabPluginFilterParametrization STATIC
    parametrizationfilterplugin.h
    parametrizationfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterParametrization
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterParametrization
    PUBLIC
        QMeshLabCore
        QMeshLabPluginFilterIglCommon
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterParametrization "filter_parametrization_descriptors"
    PREFIX "/filters/qmeshlab.filter.parametrization"
    FILES filters.json
)
