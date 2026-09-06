option(QMESH_PLUGIN_FILTER_ISOPARAM "Build the isoparametrization filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_ISOPARAM)
    message(STATUS "Skipping filter_isoparam - plugin disabled")
    return()
endif()

add_library(QMeshLabPluginFilterIsoParam STATIC
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

# The 2010 reference code still derives from std::unary_function, which C++17 removed.
# libc++ keeps it behind this switch; scoped to this target so nothing else sees it.
target_compile_definitions(QMeshLabPluginFilterIsoParam
    PRIVATE _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION)

target_link_libraries(QMeshLabPluginFilterIsoParam
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterIsoParam "filter_isoparam_descriptors"
    PREFIX "/filters/qmeshlab.filter.isoparam"
    FILES filters.json
)
