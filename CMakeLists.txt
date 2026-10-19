cmake_minimum_required(VERSION 3.21)

project(LumiCanvas VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "LumiCanvas is a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent)
qt_standard_project_setup()

qt_add_executable(LumiCanvas WIN32
    src/main.cpp
    src/core/ImageLoader.h
    src/core/ImageLoader.cpp
    src/core/Project.h
    src/core/Project.cpp
    src/core/Sequence.h
    src/core/Sequence.cpp
    src/render/RenderEngine.h
    src/render/RenderEngine.cpp
    src/render/OutputWindow.h
    src/render/OutputWindow.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/WarpEditor.h
    src/ui/WarpEditor.cpp
)

qt_add_resources(LumiCanvas "shaders"
    PREFIX "/shaders"
    BASE "resources/shaders"
    FILES resources/shaders/warp.hlsl
)

target_include_directories(LumiCanvas PRIVATE src)

target_compile_definitions(LumiCanvas PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    LUMI_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(LumiCanvas PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

target_link_libraries(LumiCanvas PRIVATE Qt6::Widgets Qt6::Concurrent d3d11 dxgi d3dcompiler)

# Copy the Qt runtime next to the executable so it runs straight from the build tree.
option(LUMI_DEPLOY_QT "Run windeployqt after each build" ON)
if(LUMI_DEPLOY_QT AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET LumiCanvas POST_BUILD
        COMMAND Qt6::windeployqt
            --no-translations --no-compiler-runtime --no-opengl-sw
            --no-system-d3d-compiler --verbose 0
            "$<TARGET_FILE:LumiCanvas>"
        COMMENT "Deploying Qt runtime"
        VERBATIM
    )
endif()
