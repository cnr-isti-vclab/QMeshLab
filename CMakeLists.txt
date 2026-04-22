cmake_minimum_required(VERSION 3.25)
project(QMeshLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(QT_NO_PRIVATE_MODULE_WARNING ON)

option(QMESH_REQUIRE_VCPKG_DEPS "Require vcpkg toolchain for non-Qt external dependencies" ON)
if (QMESH_REQUIRE_VCPKG_DEPS)
    if (NOT DEFINED VCPKG_TARGET_TRIPLET)
        message(FATAL_ERROR
            "QMESH_REQUIRE_VCPKG_DEPS=ON but vcpkg toolchain is not active. "
            "Set VCPKG_ROOT to your vcpkg clone and configure with "
            "-DCMAKE_TOOLCHAIN_FILE=$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake, "
            "or use the 'vcpkg-manifest' preset.")
    endif()
    message(STATUS "QMeshLab external dependency mode: vcpkg (triplet: ${VCPKG_TARGET_TRIPLET})")
endif()

find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets GuiPrivate ShaderTools Test)
add_library(QMeshLabCore STATIC
    colormap.h colormap.cpp
    document.h document.cpp
    linerenderer.h linerenderer.cpp
    meshfilterplugin.h
    meshfilterpluginmanager.h meshfilterpluginmanager.cpp
    meshgpuresourcecache.h meshgpuresourcecache.cpp
    meshioplugin.h
    meshiopluginmanager.h meshiopluginmanager.cpp
    vcgmesh.h
)

target_include_directories(QMeshLabCore PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vcglib
    ${CMAKE_SOURCE_DIR}/vcglib/eigenlib
)

target_link_libraries(QMeshLabCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
)

qt_add_resources(QMeshLabCore "colormaps"
    PREFIX "/"
    FILES
        colormaps/rainbow.json
        colormaps/viridis.json
        colormaps/gray.json
        colormaps/plasma.json
        colormaps/cividis.json
        colormaps/turbo.json
)

add_subdirectory(plugins)

qt_add_executable(QMeshLab
    main.cpp
    mainwindow.h mainwindow.cpp
    meshfilterpanel.h meshfilterpanel.cpp
    meshsaveoptionsdialog.h meshsaveoptionsdialog.cpp
    renderwidget.h renderwidget.cpp
    renderwidget_internal.h
    renderwidget_modes.cpp
    renderwidget_resources.cpp
    renderwidget_selection.cpp
    renderwidget_render.cpp
    renderwidget_uv.cpp
    viewtrackball.h viewtrackball.cpp
    renderoverlaypanel.h renderoverlaypanel.cpp
    layerwidget.h layerwidget.cpp
)

set_target_properties(QMeshLab PROPERTIES
    MACOSX_BUNDLE TRUE
)

target_link_libraries(QMeshLab PRIVATE
    QMeshLabPlugins
    Qt6::Widgets
    Qt6::GuiPrivate
)

qt_add_shaders(QMeshLab "shaders"
    PREFIX "/"
    FILES
        shaders/fill_smooth.vert
        shaders/fill_smooth.frag
        shaders/fill_flat.vert
        shaders/fill_flat.frag
        shaders/fill_radscale.frag
        shaders/fill_radscale_buf.frag
        shaders/fill_wire.vert
        shaders/fill_wire_overlay.frag
        shaders/overlay_bbox.vert
        shaders/overlay_bbox.frag
        shaders/overlay_edges.vert
        shaders/overlay_edges.frag
        shaders/overlay_fat_edges.vert
        shaders/overlay_fat_edges.frag
        shaders/overlay_fat_decorator.vert
        shaders/overlay_fat_decorator.frag
        shaders/overlay_points.vert
        shaders/overlay_points.frag
        shaders/overlay_decorator.vert
        shaders/overlay_decorator.frag
        shaders/overlay_trackball_gizmo.vert
        shaders/overlay_trackball_gizmo.frag
        shaders/selection_mask.vert
        shaders/selection_mask.frag
        shaders/depth_pick.vert
        shaders/depth_pick.frag
        shaders/selection_mask_morph.frag
        shaders/selection_mask_debug.frag
        shaders/selection_outline.vert
        shaders/selection_outline_extract.frag
        shaders/selection_outline.frag
        shaders/scene_background.vert
        shaders/scene_background.frag
        shaders/uv_background.vert
        shaders/uv_background.frag
        shaders/uv_fill_texture.vert
)

qt_add_resources(QMeshLab "icons"
    PREFIX "/"
    FILES
        img/options.png
        img/global.png
        img/normals.png
        img/boundary.png
        img/box.png
        img/points.png
        img/edge-mesh.png
        img/wireframe.png
        img/wire.png
        img/flat.png
        img/selected.png
        img/sel_all.png
        img/sel_none.png
        img/sel_plus.png
        img/sel_minus.png
        img/sel_inv.png
        img/delete_vert.png
        img/delete_face.png
        img/delete_facevert.png
        img/histogram.png
        img/splitH.png
        img/splitV.png
        img/MeshLab_Icon_512x512.png
)

enable_testing()

qt_add_executable(QMeshLabTests
    tests/test_document.cpp
)

target_include_directories(QMeshLabTests PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(QMeshLabTests PRIVATE
    QMeshLabPlugins
    Qt6::Test
)

target_compile_definitions(QMeshLabTests PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME QMeshLabTests COMMAND QMeshLabTests)

qt_add_executable(QMeshLabColorMapTests
    tests/test_colormap.cpp
)

target_include_directories(QMeshLabColorMapTests PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(QMeshLabColorMapTests PRIVATE
    QMeshLabCore
    Qt6::Test
)

add_test(NAME QMeshLabColorMapTests COMMAND QMeshLabColorMapTests)

qt_add_executable(QMeshLabFilterTests
    tests/test_filters.cpp
)

target_include_directories(QMeshLabFilterTests PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(QMeshLabFilterTests PRIVATE
    QMeshLabPlugins
    Qt6::Test
)

target_compile_definitions(QMeshLabFilterTests PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME QMeshLabFilterTests COMMAND QMeshLabFilterTests)
