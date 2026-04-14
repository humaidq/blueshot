cmake_minimum_required(VERSION 3.25)

project(BlueshotEditorQt VERSION 0.1.0 LANGUAGES CXX)

set(BLUESHOT_APP_ID "ae.humaidq.blueshot")
set(BLUESHOT_EXECUTABLE_NAME "Blueshot.Editor.Qt" CACHE STRING "Executable name used in the Linux desktop entry")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

set(blueshot_executable_type)
if(WIN32)
    list(APPEND blueshot_executable_type WIN32)
endif()
if(APPLE)
    list(APPEND blueshot_executable_type MACOSX_BUNDLE)
endif()

get_filename_component(QT_CMAKE_DIR "${Qt6_DIR}" DIRECTORY)
get_filename_component(QT_LIB_DIR "${QT_CMAKE_DIR}" DIRECTORY)
get_filename_component(QT_PREFIX_DIR "${QT_LIB_DIR}" DIRECTORY)
set(QT_INCLUDE_ROOT "${QT_PREFIX_DIR}/include")

qt_add_executable(Blueshot.Editor.Qt
    ${blueshot_executable_type}
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

set_target_properties(Blueshot.Editor.Qt PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Blueshot"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 Humaid Alqasimi"
    MACOSX_BUNDLE_GUI_IDENTIFIER ${BLUESHOT_APP_ID}
    MACOSX_BUNDLE_INFO_STRING "Blueshot screenshot editor"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE
)

if(UNIX AND NOT APPLE)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/ae.humaidq.blueshot.desktop.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${BLUESHOT_APP_ID}.desktop"
        @ONLY
    )
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Blueshot.Editor.Qt)
endif()

install(TARGETS Blueshot.Editor.Qt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

if(UNIX AND NOT APPLE)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${BLUESHOT_APP_ID}.desktop"
        DESTINATION share/applications
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Blueshot/icons/applicationIcon/48.png"
        DESTINATION share/icons/hicolor/48x48/apps
        RENAME "${BLUESHOT_APP_ID}.png"
    )
endif()
