cmake_minimum_required(VERSION 3.25)
project(QMeshLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(QT_NO_PRIVATE_MODULE_WARNING ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets GuiPrivate ShaderTools Test)
add_library(QMeshLabCore STATIC
    document.h document.cpp
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
)

add_subdirectory(plugins)

qt_add_executable(QMeshLab
    main.cpp
    mainwindow.h mainwindow.cpp
    renderwidget.h renderwidget.cpp
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
        shaders/color.vert
        shaders/color.frag
        shaders/flat.vert
        shaders/flat.frag
        shaders/wireframe.vert
        shaders/wireframe.frag
        shaders/wireframe_lines.frag
        shaders/bbox.vert
        shaders/bbox.frag
        shaders/points.vert
        shaders/points.frag
)

qt_add_resources(QMeshLab "icons"
    PREFIX "/"
    FILES
        img/options.png
        img/box.png
        img/points.png
        img/wire.png
        img/flat.png
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
