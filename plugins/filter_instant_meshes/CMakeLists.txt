option(QMESH_PLUGIN_FILTER_INSTANT_MESHES "Build the Instant Meshes remeshing plugin" ON)

if (NOT QMESH_PLUGIN_FILTER_INSTANT_MESHES)
    message(STATUS "Skipping filter_instant_meshes - plugin disabled")
    return()
endif()

find_package(Eigen3 CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)

set(INSTANT_MESHES_SRC "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src")
add_library(QMeshLabInstantMeshesCore STATIC
    "${INSTANT_MESHES_SRC}/adjacency.cpp"
    "${INSTANT_MESHES_SRC}/bvh.cpp"
    "${INSTANT_MESHES_SRC}/cleanup.cpp"
    "${INSTANT_MESHES_SRC}/dedge.cpp"
    "${INSTANT_MESHES_SRC}/extract.cpp"
    "${INSTANT_MESHES_SRC}/field.cpp"
    "${INSTANT_MESHES_SRC}/hierarchy.cpp"
    "${INSTANT_MESHES_SRC}/meshstats.cpp"
    "${INSTANT_MESHES_SRC}/normal.cpp"
    "${INSTANT_MESHES_SRC}/reorder.cpp"
    "${INSTANT_MESHES_SRC}/subdivide.cpp"
)
target_include_directories(QMeshLabInstantMeshesCore PUBLIC
    "${INSTANT_MESHES_SRC}"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/ext/dset"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/ext/pcg32"
)
target_link_libraries(QMeshLabInstantMeshesCore PUBLIC Eigen3::Eigen TBB::tbb)

# The adapter compiles against vcglib's bundled Eigen 3.4.0 (QMeshLabCore exports that
# include dir publicly), so the core must use the same one: otherwise the two halves of
# this plugin disagree on Eigen's internals and the matrices the adapter builds are
# reinterpreted with a different layout by the core.
target_include_directories(QMeshLabInstantMeshesCore BEFORE PUBLIC
    ${CMAKE_SOURCE_DIR}/vcglib/eigenlib)
target_compile_features(QMeshLabInstantMeshesCore PUBLIC cxx_std_17)
if (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(QMeshLabInstantMeshesCore PRIVATE
        -Wno-deprecated-declarations
        -Wno-unused-parameter
    )
endif()

add_library(QMeshLabPluginFilterInstantMeshes STATIC
    instantmeshes_adapter.h
    instantmeshes_adapter.cpp
    instantmeshesfilterplugin.h
    instantmeshesfilterplugin.cpp
)
target_include_directories(QMeshLabPluginFilterInstantMeshes PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(QMeshLabPluginFilterInstantMeshes PUBLIC
    QMeshLabCore
    QMeshLabInstantMeshesCore
    Qt6::Core
)

qt_add_resources(QMeshLabPluginFilterInstantMeshes "filter_instant_meshes_descriptors"
    PREFIX "/filters/qmeshlab.filter.instant_meshes"
    FILES filters.json
)
