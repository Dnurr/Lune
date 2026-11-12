cmake_minimum_required(VERSION 3.21)
project(Lune LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets ShaderTools)

add_executable(Lune
    # Engine: scene data and rendering, no Qt Widgets
    src/engine/scene/scene.cpp
    src/engine/render/camera.cpp
    src/engine/render/renderer.cpp

    # Frontend: editor UI
    src/frontend/core/selection.cpp
    src/frontend/core/commands.cpp
    src/frontend/components/viewport/viewport.cpp
    src/frontend/components/widgets/draglabel.cpp
    src/frontend/components/widgets/vector3.cpp
    src/frontend/components/panel/panel.cpp
    src/frontend/components/panel/transform/transform.cpp
    src/frontend/components/panel/outliner/outliner.cpp
    src/frontend/editor.cpp
    src/frontend/theme/theme.cpp
    src/main.cpp
)
# Includes are relative to src/: "engine/scene/scene.h", "frontend/core/selection.h"
target_include_directories(Lune PRIVATE src)
# QRhi headers (<rhi/qrhi.h>) are semi-public and ship with the GuiPrivate target
target_link_libraries(Lune PRIVATE Qt6::Widgets Qt6::GuiPrivate)

# Shaders: compiled at build time by qsb (Qt Shader Tools) for every QRhi backend,
# embedded as resources at :/shaders/<name>.qsb
qt6_add_shaders(Lune "shaders"
    PREFIX "/shaders"
    BASE "src/engine/render/shaders"
    GLSL "300es,330"
    HLSL 50
    MSL 12
    FILES
        src/engine/render/shaders/fullscreen.vert
        src/engine/render/shaders/fullscreen_flipped.vert
        src/engine/render/shaders/grid.frag
        src/engine/render/shaders/downsample.frag
        src/engine/render/shaders/blur.frag
        src/engine/render/shaders/composite.frag
)

if(WIN32)
    find_program(WINDEPLOYQT windeployqt
        HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT)
        add_custom_command(TARGET Lune POST_BUILD
            COMMAND "${WINDEPLOYQT}" --no-translations --no-opengl-sw
                    --no-system-d3d-compiler "$<TARGET_FILE:Lune>")
    endif()
endif()