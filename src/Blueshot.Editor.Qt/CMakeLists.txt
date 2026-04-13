cmake_minimum_required(VERSION 3.25)

project(BlueshotEditorQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

get_filename_component(QT_CMAKE_DIR "${Qt6_DIR}" DIRECTORY)
get_filename_component(QT_LIB_DIR "${QT_CMAKE_DIR}" DIRECTORY)
get_filename_component(QT_PREFIX_DIR "${QT_LIB_DIR}" DIRECTORY)
set(QT_INCLUDE_ROOT "${QT_PREFIX_DIR}/include")

qt_add_executable(Blueshot.Editor.Qt
    CanvasWidget.cpp
    CanvasWidget.h
    main.cpp
    MainWindow.cpp
    MainWindow.h
    resources.qrc
)

target_link_libraries(Blueshot.Editor.Qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_include_directories(Blueshot.Editor.Qt PRIVATE
    "${QT_INCLUDE_ROOT}"
)

target_compile_options(Blueshot.Editor.Qt PRIVATE
    "-I${QT_INCLUDE_ROOT}"
)

target_compile_definitions(Blueshot.Editor.Qt PRIVATE
    QT_NO_KEYWORDS
)

install(TARGETS Blueshot.Editor.Qt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
