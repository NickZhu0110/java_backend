cmake_minimum_required(VERSION 3.19)
project(qt-cac-app LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Concurrent
    Widgets
    OpenGLWidgets
    Network
    WebSockets
)

find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    FiltersCore
    FiltersGeneral
    FiltersSources
    GUISupportQt
    ImagingCore
    IOImage
    InteractionStyle
    InteractionWidgets
    RenderingAnnotation
    RenderingCore
    RenderingOpenGL2
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
    src/cache/JobFileCacheManager.cpp
    src/cache/JobFileCacheManager.h
    src/io/BackendFileClient.cpp
    src/io/BackendFileClient.h
    src/data/MaskVolume.h
    src/data/VolumeData.h
    src/viewer/CaseVolumeLoader.cpp
    src/viewer/CaseVolumeLoader.h
    src/viewer/CTViewerWidget.cpp
    src/viewer/CTViewerWidget.h
    src/viewer/LoadedCaseVolume.h
    src/viewer/Mask3DViewerWidget.cpp
    src/viewer/Mask3DViewerWidget.h
    src/viewer/MultiStructureNiftiLoader.cpp
    src/viewer/MultiStructureNiftiLoader.h
    src/viewer/MultiStructureVolume.h
    src/viewer/PythonSimpleItkPreprocessor.cpp
    src/viewer/PythonSimpleItkPreprocessor.h
    src/viewer/RawVolumeLoader.cpp
    src/viewer/RawVolumeLoader.h
)

target_include_directories(qt-cac-app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(qt-cac-app
    PRIVATE
        Qt::Core
        Qt::Concurrent
        Qt::Widgets
        Qt::OpenGLWidgets
        Qt::Network
        Qt::WebSockets
        ${VTK_LIBRARIES}
)

if (VTK_VERSION VERSION_GREATER_EQUAL "8.90.0")
    vtk_module_autoinit(
        TARGETS qt-cac-app
        MODULES ${VTK_LIBRARIES}
    )
endif()

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
