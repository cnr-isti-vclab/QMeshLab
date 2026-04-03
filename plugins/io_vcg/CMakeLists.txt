add_library(QMeshLabPluginIOVCG STATIC
    vcgimportplugin.h
    vcgimportplugin.cpp
    ${CMAKE_SOURCE_DIR}/vcglib/wrap/ply/plylib.cpp
)

target_include_directories(QMeshLabPluginIOVCG
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vcglib
        ${CMAKE_SOURCE_DIR}/vcglib/eigenlib
)

target_link_libraries(QMeshLabPluginIOVCG
    PUBLIC
        QMeshLabCore
        Qt6::Core
)
