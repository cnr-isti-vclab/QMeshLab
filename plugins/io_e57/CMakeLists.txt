option(QMESH_PLUGIN_E57 "Build E57 input plugin" ON)
option(QMESH_PLUGIN_E57_FETCH_LIBE57FORMAT "Fetch libE57Format from GitHub when not installed" ON)

if (NOT QMESH_PLUGIN_E57)
    message(STATUS "Skipping io_e57 - plugin disabled")
    return()
endif()

find_package(XercesC QUIET)
find_package(E57Format CONFIG QUIET)

if (NOT E57Format_FOUND AND QMESH_PLUGIN_E57_FETCH_LIBE57FORMAT)
    if (NOT XercesC_FOUND)
        message(STATUS "Skipping io_e57 - missing XercesC required by libE57Format")
        return()
    endif()

    include(FetchContent)
    set(E57_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(E57_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libE57Format
        GIT_REPOSITORY https://github.com/asmaloney/libE57Format.git
        GIT_TAG v3.3.0
    )
    FetchContent_MakeAvailable(libE57Format)
endif()

if (NOT TARGET E57Format)
    message(STATUS "Skipping io_e57 - missing libE57Format target")
    return()
endif()

add_library(QMeshLabPluginIOE57 STATIC
    e57importplugin.h
    e57importplugin.cpp
)

target_include_directories(QMeshLabPluginIOE57
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vcglib
        ${CMAKE_SOURCE_DIR}/vcglib/eigenlib
)

target_link_libraries(QMeshLabPluginIOE57
    PUBLIC
        QMeshLabCore
        Qt6::Core
        E57Format
)
