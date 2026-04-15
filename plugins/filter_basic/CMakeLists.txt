option(QMESH_PLUGIN_FILTER_BASIC "Build basic mesh filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_BASIC)
    message(STATUS "Skipping filter_basic - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterBasic STATIC
    basicfilterplugin.h
    basicfilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterBasic
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QMeshLabPluginFilterBasic
    PUBLIC
        QMeshLabCore
        Qt6::Core
)
