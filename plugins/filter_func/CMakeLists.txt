option(QMESH_PLUGIN_FILTER_FUNC "Build expression-based mesh filter plugin (muparser)" ON)

if (NOT QMESH_PLUGIN_FILTER_FUNC)
    message(STATUS "Skipping filter_func - plugin disabled")
    return()
endif()

set(_muparser_target "")
find_package(muparser CONFIG QUIET)
if (TARGET muparser::muparser)
    set(_muparser_target muparser::muparser)
elseif(TARGET muparser)
    set(_muparser_target muparser)
elseif(TARGET unofficial::muparser::muparser)
    set(_muparser_target unofficial::muparser::muparser)
endif()

set(MUPARSER_INCLUDE_DIR "")
set(MUPARSER_LIBRARY "")
if (NOT _muparser_target)
    find_path(MUPARSER_INCLUDE_DIR muParser.h)
    find_library(MUPARSER_LIBRARY NAMES muparser)
endif()

if (NOT _muparser_target AND (NOT MUPARSER_INCLUDE_DIR OR NOT MUPARSER_LIBRARY))
    message(FATAL_ERROR
        "filter_func requires muparser from vcpkg. "
        "Add muparser to vcpkg.json dependencies and configure with the vcpkg toolchain.")
endif()

add_library(QMeshLabPluginFilterFunc STATIC
    funcfilterplugin.h
    funcfilterplugin.cpp
    filter_refine.h
)

target_include_directories(QMeshLabPluginFilterFunc
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (MUPARSER_INCLUDE_DIR)
    target_include_directories(QMeshLabPluginFilterFunc PUBLIC ${MUPARSER_INCLUDE_DIR})
endif()

target_link_libraries(QMeshLabPluginFilterFunc
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

if (_muparser_target)
    target_link_libraries(QMeshLabPluginFilterFunc PUBLIC ${_muparser_target})
else()
    target_link_libraries(QMeshLabPluginFilterFunc PUBLIC ${MUPARSER_LIBRARY})
endif()


qt_add_resources(QMeshLabPluginFilterFunc "filter_func_descriptors"
    PREFIX "/filters/qmeshlab.filter.func"
    FILES filters.json
)
