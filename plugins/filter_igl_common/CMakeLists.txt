option(QMESH_PLUGIN_FILTER_IGL "Build libigl-based mesh filter support" ON)

if (NOT QMESH_PLUGIN_FILTER_IGL)
    message(STATUS "Skipping libigl filter support - plugin family disabled")
    return()
endif()

find_package(libigl CONFIG QUIET)

set(QMESH_IGL_CORE_TARGET "")
foreach(candidate IN ITEMS igl::core igl::igl_core)
    if (TARGET ${candidate})
        set(QMESH_IGL_CORE_TARGET ${candidate})
        break()
    endif()
endforeach()

if (QMESH_IGL_CORE_TARGET STREQUAL "")
    message(STATUS "Skipping libigl filter support - libigl core target not found")
    return()
endif()

add_library(QMeshLabPluginFilterIglCommon STATIC
    libiglmeshadapter.h
    libiglmeshadapter.cpp
)

target_include_directories(QMeshLabPluginFilterIglCommon
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterIglCommon
    PUBLIC
        QMeshLabCore
        Qt6::Core
        Qt6::Gui
        ${QMESH_IGL_CORE_TARGET}
)
