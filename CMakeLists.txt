cmake_minimum_required(VERSION 3.25)
project(QMeshLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets GuiPrivate ShaderTools)

qt_add_executable(QMeshLab
    main.cpp
    mainwindow.h mainwindow.cpp
    renderwidget.h renderwidget.cpp
    document.h document.cpp
    layerwidget.h layerwidget.cpp
    vcgmesh.h
    vcglib/wrap/ply/plylib.cpp
)

target_include_directories(QMeshLab PRIVATE
    ${CMAKE_SOURCE_DIR}/vcglib
    ${CMAKE_SOURCE_DIR}/vcglib/eigenlib
)

target_link_libraries(QMeshLab PRIVATE
    Qt6::Widgets
    Qt6::GuiPrivate
)

qt_add_shaders(QMeshLab "shaders"
    PREFIX "/"
    FILES
        shaders/color.vert
        shaders/color.frag
)
