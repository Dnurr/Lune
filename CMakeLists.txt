cmake_minimum_required(VERSION 3.21)
project(Lune LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets ShaderTools)

add_executable(Lune
    src/frontend/components/renderer/renderer.cpp
    src/frontend/components/panel/panel.cpp
    src/frontend/components/panel/transform/transform.cpp
    src/frontend/editor.cpp
    src/frontend/theme/theme.cpp
    src/main.cpp
)
# QRhi headers (<rhi/qrhi.h>) are semi-public and ship with the GuiPrivate target
target_link_libraries(Lune PRIVATE Qt6::Widgets Qt6::GuiPrivate)

# Shaders: compiled at build time by qsb (Qt Shader Tools) for every QRhi backend,
# embedded as resources at :/shaders/<name>.qsb
qt6_add_shaders(Lune "shaders"
    PREFIX "/shaders"
    BASE "src/frontend/components/renderer/shaders"
    GLSL "300es,330"
    HLSL 50
    MSL 12
    FILES
        src/frontend/components/renderer/shaders/fullscreen.vert
        src/frontend/components/renderer/shaders/fullscreen_flipped.vert
        src/frontend/components/renderer/shaders/grid.frag
        src/frontend/components/renderer/shaders/downsample.frag
        src/frontend/components/renderer/shaders/blur.frag
        src/frontend/components/renderer/shaders/composite.frag
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