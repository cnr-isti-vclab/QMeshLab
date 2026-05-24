option(QMESH_PLUGIN_FILTER_MESH_BOOLEANS "Build libigl/CGAL mesh boolean filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_MESH_BOOLEANS)
    message(STATUS "Skipping filter_mesh_booleans - plugin disabled")
    return()
endif()

if (NOT TARGET QMeshLabPluginFilterIglCommon)
    message(STATUS "Skipping filter_mesh_booleans - libigl common support not available")
    return()
endif()

find_package(libigl CONFIG QUIET)

set(QMESH_IGL_COPYLEFT_CGAL_TARGET "")
foreach(candidate IN ITEMS igl_copyleft::cgal igl_copyleft::igl_copyleft_cgal igl::copyleft_cgal igl_copyleft_cgal)
    if (TARGET ${candidate})
        set(QMESH_IGL_COPYLEFT_CGAL_TARGET ${candidate})
        break()
    endif()
endforeach()

if (QMESH_IGL_COPYLEFT_CGAL_TARGET STREQUAL "")
    message(STATUS "Skipping filter_mesh_booleans - libigl copyleft CGAL target not found")
    return()
endif()

add_library(QMeshLabPluginFilterMeshBooleans STATIC
    meshbooleansfilterplugin.h
    meshbooleansfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterMeshBooleans
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterMeshBooleans
    PUBLIC
        QMeshLabCore
        QMeshLabPluginFilterIglCommon
        Qt6::Core
        ${QMESH_IGL_COPYLEFT_CGAL_TARGET}
)

qt_add_resources(QMeshLabPluginFilterMeshBooleans "filter_mesh_booleans_descriptors"
    PREFIX "/filters/qmeshlab.filter.mesh_booleans"
    FILES filters.json
)
