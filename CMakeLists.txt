cmake_minimum_required(VERSION 3.21)

project(LumiCanvas VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "LumiCanvas is a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent LinguistTools)
qt_standard_project_setup()

qt_add_executable(LumiCanvas WIN32
    src/main.cpp
    src/core/History.h
    src/core/History.cpp
    src/core/ImageLoader.h
    src/core/ImageLoader.cpp
    src/core/Project.h
    src/core/Project.cpp
    src/core/Sequence.h
    src/core/Sequence.cpp
    src/core/StepGenerator.h
    src/core/StepGenerator.cpp
    src/render/RenderEngine.h
    src/render/RenderEngine.cpp
    src/render/OutputWindow.h
    src/render/OutputWindow.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/GenerateStepsDialog.h
    src/ui/GenerateStepsDialog.cpp
    src/ui/KeySettingsDialog.h
    src/ui/KeySettingsDialog.cpp
    src/ui/WarpEditor.h
    src/ui/WarpEditor.cpp
    resources/lumicanvas.rc
)

qt_add_resources(LumiCanvas "shaders"
    PREFIX "/shaders"
    BASE "resources/shaders"
    FILES resources/shaders/warp.hlsl
)

qt_add_resources(LumiCanvas "icons"
    PREFIX "/icons"
    BASE "resources/icons"
    FILES resources/icons/lumicanvas.ico
)

# UI translations, embedded as :/i18n/lumicanvas_<lang>.qm.
# Refresh the .ts files after changing UI strings: cmake --build build --target update_translations
qt_add_translations(LumiCanvas
    TS_FILES translations/lumicanvas_ko.ts
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations none
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

# Tests: cmake --build --preset debug --target test_stepgenerator, then ctest --test-dir build -C Debug
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(test_stepgenerator
    tests/test_stepgenerator.cpp
    src/core/ImageLoader.cpp
    src/core/StepGenerator.cpp
)
set_target_properties(test_stepgenerator PROPERTIES WIN32_EXECUTABLE FALSE)
target_include_directories(test_stepgenerator PRIVATE src)
target_compile_definitions(test_stepgenerator PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(test_stepgenerator PRIVATE /utf-8)
target_link_libraries(test_stepgenerator PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME stepgenerator COMMAND test_stepgenerator)
set_tests_properties(stepgenerator PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

# Copy the Qt runtime next to the executable so it runs straight from the build tree.
option(LUMI_DEPLOY_QT "Run windeployqt after each build" ON)
if(LUMI_DEPLOY_QT AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET LumiCanvas POST_BUILD
        COMMAND Qt6::windeployqt
            --translations ko --no-compiler-runtime --no-opengl-sw
            --no-system-d3d-compiler --verbose 0
            "$<TARGET_FILE:LumiCanvas>"
        COMMENT "Deploying Qt runtime"
        VERBATIM
    )
endif()
