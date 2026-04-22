option(QMESH_PLUGIN_FILTER_EMBREE "Build Embree-based mesh filter plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_EMBREE)
    message(STATUS "Skipping filter_embree - plugin disabled")
    return()
endif()

set(_embree_target "")
find_package(embree CONFIG QUIET)
if (TARGET embree::embree)
    set(_embree_target embree::embree)
elseif(TARGET Embree::embree)
    set(_embree_target Embree::embree)
elseif(TARGET embree)
    set(_embree_target embree)
elseif(TARGET embree4)
    set(_embree_target embree4)
endif()

set(EMBREE_INCLUDE_DIR "")
set(EMBREE_LIBRARY "")
if (NOT _embree_target)
    find_path(EMBREE_INCLUDE_DIR embree4/rtcore.h)
    find_library(EMBREE_LIBRARY NAMES embree4 embree)
endif()

if (NOT _embree_target AND (NOT EMBREE_INCLUDE_DIR OR NOT EMBREE_LIBRARY))
    message(FATAL_ERROR
        "filter_embree requires embree (v4). "
        "Add embree to vcpkg.json dependencies and configure with the vcpkg toolchain.")
endif()

find_package(OpenMP QUIET COMPONENTS CXX)
if (NOT OpenMP_CXX_FOUND AND APPLE)
    set(_openmp_root_hints "")
    if (DEFINED OpenMP_ROOT AND OpenMP_ROOT)
        list(APPEND _openmp_root_hints "${OpenMP_ROOT}")
    endif()
    if (DEFINED ENV{OpenMP_ROOT} AND NOT "$ENV{OpenMP_ROOT}" STREQUAL "")
        list(APPEND _openmp_root_hints "$ENV{OpenMP_ROOT}")
    endif()
    list(APPEND _openmp_root_hints /opt/homebrew/opt/libomp /usr/local/opt/libomp)

    find_path(_openmp_include_dir
        NAMES omp.h
        HINTS ${_openmp_root_hints}
        PATH_SUFFIXES include
    )
    find_library(_openmp_library
        NAMES omp libomp
        HINTS ${_openmp_root_hints}
        PATH_SUFFIXES lib
    )

    if (_openmp_include_dir AND _openmp_library)
        add_library(OpenMP::OpenMP_CXX INTERFACE IMPORTED)
        set_target_properties(OpenMP::OpenMP_CXX PROPERTIES
            INTERFACE_COMPILE_OPTIONS "-Xclang;-fopenmp"
            INTERFACE_INCLUDE_DIRECTORIES "${_openmp_include_dir}"
            INTERFACE_LINK_LIBRARIES "${_openmp_library}"
        )
        set(OpenMP_CXX_FOUND TRUE)
    endif()
endif()

if (NOT OpenMP_CXX_FOUND)
    message(STATUS
        "Skipping filter_embree - OpenMP CXX was not found, "
        "and vcglib's EmbreeAdaptor requires omp.h")
    return()
endif()

add_library(QMeshLabPluginFilterEmbree STATIC
    embreefilterplugin.h
    embreefilterplugin.cpp
)

target_include_directories(QMeshLabPluginFilterEmbree
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (EMBREE_INCLUDE_DIR)
    target_include_directories(QMeshLabPluginFilterEmbree PUBLIC ${EMBREE_INCLUDE_DIR})
endif()

target_link_libraries(QMeshLabPluginFilterEmbree
    PUBLIC
        QMeshLabCore
        Qt6::Core
)

if (_embree_target)
    target_link_libraries(QMeshLabPluginFilterEmbree PUBLIC ${_embree_target})
else()
    target_link_libraries(QMeshLabPluginFilterEmbree PUBLIC ${EMBREE_LIBRARY})
endif()

target_link_libraries(QMeshLabPluginFilterEmbree PUBLIC OpenMP::OpenMP_CXX)
