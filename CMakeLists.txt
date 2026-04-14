cmake_minimum_required(VERSION 3.25)
project(QMeshLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(QT_NO_PRIVATE_MODULE_WARNING ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets GuiPrivate ShaderTools Test)
add_library(QMeshLabCore STATIC
    colormap.h colormap.cpp
    document.h document.cpp
    linerenderer.h linerenderer.cpp
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

target_link_libraries(QMeshLab PRIVATE
    QMeshLabCore
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
    QMeshLabCore
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
