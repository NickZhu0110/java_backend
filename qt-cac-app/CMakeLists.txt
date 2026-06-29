cmake_minimum_required(VERSION 3.19)
project(qt-cac-app LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    WebSockets
)

qt_standard_project_setup()

qt_add_executable(qt-cac-app
    WIN32 MACOSX_BUNDLE
    main.cpp
    BackendClient.cpp
    BackendClient.h
    JobWebSocketClient.cpp
    JobWebSocketClient.h
    ServerSettingsDialog.cpp
    ServerSettingsDialog.h
    ServerSettingsDialog.ui
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    src/viewer/CTViewerWidget.cpp
    src/viewer/CTViewerWidget.h
)

target_include_directories(qt-cac-app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(qt-cac-app
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::WebSockets
)

install(TARGETS qt-cac-app
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET qt-cac-app
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)

install(SCRIPT ${deploy_script})
