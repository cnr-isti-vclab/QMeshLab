cmake_minimum_required(VERSION 3.25)
project(QMeshLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(QT_NO_PRIVATE_MODULE_WARNING ON)

option(QMESH_REQUIRE_VCPKG_DEPS "Require vcpkg toolchain for non-Qt external dependencies" ON)
option(QMESHLAB_MATH_HELP "Render LaTeX formulas in filter help" ON)
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

if(QMESHLAB_MATH_HELP)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Xml)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/jkqtplotter/CMakeLists.txt")
        message(FATAL_ERROR
            "QMESHLAB_MATH_HELP=ON but external/jkqtplotter is missing. "
            "Run: git submodule update --init external/jkqtplotter")
    endif()
    set(JKQtPlotter_BUILD_INCLUDE_XITS_FONTS OFF)
    set(JKQtPlotter_BUILD_INCLUDE_FIRAMATH_FONTS ON)
    set(JKQtPlotter_BUILD_LIB_JKQTMATH OFF)
    set(JKQtPlotter_BUILD_LIB_JKQTMATHTEXT ON)
    set(JKQtPlotter_BUILD_LIB_JKQTFASTPLOTTER OFF)
    set(JKQtPlotter_BUILD_LIB_JKQTPLOTTER OFF)
    set(JKQtPlotter_BUILD_FORCE_NO_PRINTER_SUPPORT ON)
    set(JKQtPlotter_BUILD_WITH_PRECOMPILED_HEADERS OFF)
    if(DEFINED BUILD_SHARED_LIBS)
        set(_qmeshlab_build_shared_libs "${BUILD_SHARED_LIBS}")
    endif()
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(external/jkqtplotter EXCLUDE_FROM_ALL)
    if(DEFINED _qmeshlab_build_shared_libs)
        set(BUILD_SHARED_LIBS "${_qmeshlab_build_shared_libs}")
        unset(_qmeshlab_build_shared_libs)
    else()
        unset(BUILD_SHARED_LIBS)
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional Intel Embree ray casting (shared by core + filter_embree)
# ---------------------------------------------------------------------------
include(${CMAKE_SOURCE_DIR}/cmake/QMeshLabEmbree.cmake)

# ---------------------------------------------------------------------------
# Optional embedded Python console
# ---------------------------------------------------------------------------
option(QMESHLAB_PYTHON_CONSOLE "Build embedded Python interpreter and console dock" ON)
if(QMESHLAB_PYTHON_CONSOLE)
    # nanobind's cmake config requires the CMake `Python` package (not `Python3`)
    # because it checks for the Python::Module imported target.
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Embed Development.Module)
    # nanobind is provided by the local vcpkg ports (vcpkg/ports/nanobind).
    find_package(nanobind CONFIG REQUIRED)
    message(STATUS "QMeshLab: Python console enabled (Python ${Python_VERSION})")
endif()

add_library(QMeshLabCore STATIC
    src/core/camerashot.h src/core/camerashot.cpp
    src/core/document.h src/core/document.cpp
    src/core/document_internal.h src/core/document_internal.cpp
    src/core/documentundomanager.h src/core/documentundomanager.cpp
    src/core/document_filters.cpp
    src/core/document_gpu.cpp
    src/core/document_io.cpp
    src/core/document_layers.cpp
    src/core/document_logging.cpp
    src/core/document_memory.cpp
    src/core/document_project.cpp
    src/core/document_textures.cpp
    src/core/document_undo.cpp
    src/core/document_undo_state.cpp
    src/core/headlessrendercontext.h src/core/headlessrendercontext.cpp
    src/core/filterparam.h src/core/filterparam.cpp
    src/core/textureassociationutils.h src/core/textureassociationutils.cpp
    src/core/viewpointoccluder.h src/core/viewpointoccluder.cpp
    src/core/vcgmesh.h
    src/plugins/filtercategories.h src/plugins/filtercategories.cpp
    src/plugins/filterdescriptorloader.h src/plugins/filterdescriptorloader.cpp
    src/plugins/meshfilterplugin.h src/plugins/meshfilterplugin.cpp
    src/plugins/meshfilterpluginmanager.h src/plugins/meshfilterpluginmanager.cpp
    src/plugins/meshioplugin.h
    src/plugins/meshiopluginmanager.h src/plugins/meshiopluginmanager.cpp
    src/render/colormap.h src/render/colormap.cpp
    src/render/linerenderer.h src/render/linerenderer.cpp
    src/render/meshgpuresourcecache.h src/render/meshgpuresourcecache.cpp
    src/render/renderingsettings.h
    src/render/viewstate.h
    src/render/viewtrackball.h src/render/viewtrackball.cpp
)

target_include_directories(QMeshLabCore PUBLIC
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/render
    ${CMAKE_SOURCE_DIR}/src/plugins
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vcglib
    ${CMAKE_SOURCE_DIR}/vcglib/eigenlib
)

target_link_libraries(QMeshLabCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Widgets
)

# Embree occlusion backend (no-op interface target when embree is unavailable).
target_link_libraries(QMeshLabCore PRIVATE QMeshLabEmbree)

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

if(QMESHLAB_PYTHON_CONSOLE)
    add_subdirectory(src/python)
endif()

add_library(QMeshLabMathHelp STATIC
    src/ui/mathmarkdownrenderer.h
    src/ui/mathmarkdownrenderer.cpp
)
target_link_libraries(QMeshLabMathHelp PUBLIC Qt6::Widgets)
target_include_directories(QMeshLabMathHelp PUBLIC ${CMAKE_SOURCE_DIR}/src/ui)
if(QMESHLAB_MATH_HELP)
    target_link_libraries(QMeshLabMathHelp PRIVATE JKQTPlotter6::JKQTMathText6)
    target_compile_definitions(QMeshLabMathHelp PRIVATE QMESHLAB_MATH_HELP)
endif()

qt_add_executable(QMeshLab
    src/app/main.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
    src/ui/meshfilterpanel.h src/ui/meshfilterpanel.cpp
    src/ui/filterpluginsinfodialog.h src/ui/filterpluginsinfodialog.cpp
    src/ui/meshsaveoptionsdialog.h src/ui/meshsaveoptionsdialog.cpp
    src/render/renderwidget.h src/render/renderwidget.cpp
    src/render/renderwidget_internal.h
    src/render/renderwidget_modes.cpp
    src/render/renderwidget_resources.cpp
    src/render/renderwidget_selection.cpp
    src/render/renderwidget_frame_plan.cpp
    src/render/renderwidget_scene_passes.cpp
    src/render/renderwidget_fill.cpp
    src/render/renderwidget_render.cpp
    src/render/renderwidget_raster.cpp
    src/render/renderwidget_uv.cpp
    src/render/viewtrackball.h
    src/render/viewstate.h
    src/render/renderoverlaypanel.h src/render/renderoverlaypanel.cpp
    src/render/interactivetool.h src/render/interactivetoolfactory.cpp
    src/render/tools/selectlayertool.h src/render/tools/selectlayertool.cpp
    src/render/tools/rubberbandselecttool.h src/render/tools/rubberbandselecttool.cpp
    src/render/tools/rubberbandselecttool.h src/render/tools/rubberbandselecttool.cpp
    src/ui/layerwidget.h src/ui/layerwidget.cpp
    src/ui/undographwidget.h src/ui/undographwidget.cpp
)

set_target_properties(QMeshLab PROPERTIES
    MACOSX_BUNDLE TRUE
)

option(QMESH_MACOS_BUNDLE_ICON "Generate and embed the macOS app bundle icon" ON)
if (APPLE AND QMESH_MACOS_BUNDLE_ICON)
    set(QMESHLAB_APP_ICON_PNG "${CMAKE_SOURCE_DIR}/img/MeshLab_Icon_512x512.png")
    set(QMESHLAB_APP_ICON_ICNS "${CMAKE_CURRENT_BINARY_DIR}/QMeshLab.icns")
    add_custom_command(
        OUTPUT "${QMESHLAB_APP_ICON_ICNS}"
        COMMAND "${CMAKE_COMMAND}"
            -DINPUT_PNG=${QMESHLAB_APP_ICON_PNG}
            -DOUTPUT_ICNS=${QMESHLAB_APP_ICON_ICNS}
            -P "${CMAKE_SOURCE_DIR}/cmake/generate_mac_icon.cmake"
        DEPENDS "${QMESHLAB_APP_ICON_PNG}" "${CMAKE_SOURCE_DIR}/cmake/generate_mac_icon.cmake"
        COMMENT "Generating macOS app icon"
        VERBATIM
    )
    target_sources(QMeshLab PRIVATE "${QMESHLAB_APP_ICON_ICNS}")
    set_source_files_properties("${QMESHLAB_APP_ICON_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_target_properties(QMeshLab PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "QMeshLab.icns"
    )
endif()

target_include_directories(QMeshLab PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ui
)

target_link_libraries(QMeshLab PRIVATE
    QMeshLabPlugins
    QMeshLabMathHelp
    Qt6::Widgets
    Qt6::GuiPrivate
)

if(QMESHLAB_PYTHON_CONSOLE)
    target_sources(QMeshLab PRIVATE
        src/ui/pythonconsole.h
        src/ui/pythonconsole.cpp
        src/ui/scripteditor.h
        src/ui/scripteditor.cpp
    )
    target_link_libraries(QMeshLab PRIVATE QMeshLabPythonHost _qmeshlab_bindings)
    target_compile_definitions(QMeshLab PRIVATE QMESHLAB_PYTHON_CONSOLE)
    # Embed the Python lib dir in the rpath so the app finds libpython at
    # runtime regardless of where conda / homebrew installed it.
    set_property(TARGET QMeshLab APPEND PROPERTY
        BUILD_RPATH "${Python_LIBRARY_DIRS}")
    set_property(TARGET QMeshLab APPEND PROPERTY
        INSTALL_RPATH "${Python_LIBRARY_DIRS}")
endif()

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
        shaders/overlay_light_gizmo.vert
        shaders/overlay_light_gizmo.frag
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
        shaders/raster_backplate.vert
        shaders/raster_backplate.frag
        shaders/raster_projected.vert
        shaders/raster_projected.frag
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
        img/layer_eye_open.png
        img/layer_eye_close.png
        img/tool_select_layer.png
        img/tool_select_rect.png
        img/cur_pick.png
        img/cur_trackball.png
        img/cur_sel_rect.png
        img/cur_sel_rect_plus.png
        img/cur_sel_rect_minus.png
        img/cur_sel_rect_eye.png
        img/cur_sel_rect_plus_eye.png
        img/cur_sel_rect_minus_eye.png
)

qt_add_resources(QMeshLab "help"
    PREFIX "/"
    FILES
        resources/quick_help_overlay.html
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

if(QMESHLAB_MATH_HELP)
    qt_add_executable(QMeshLabMathMarkdownTests
        tests/test_mathmarkdown.cpp
    )
    target_link_libraries(QMeshLabMathMarkdownTests PRIVATE
        QMeshLabMathHelp
        Qt6::Test
    )
    add_test(NAME QMeshLabMathMarkdownTests COMMAND QMeshLabMathMarkdownTests)
endif()

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

qt_add_executable(QMeshLabFilterCreationTests
    tests/test_filter_creation.cpp
)

target_include_directories(QMeshLabFilterCreationTests PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(QMeshLabFilterCreationTests PRIVATE
    QMeshLabPlugins
    Qt6::Test
)

target_compile_definitions(QMeshLabFilterCreationTests PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME QMeshLabFilterCreationTests COMMAND QMeshLabFilterCreationTests)
