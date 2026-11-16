cmake_minimum_required(VERSION 3.22) # 3.22: ENVIRONMENT_MODIFICATION in tests
project(Lune LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(LUNE_BUILD_TESTS "Build the automated tests (run them with test.bat or ctest)" ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets ShaderTools)
include(cmake/dependencies.cmake)

# ---------------------------------------------------------------------------
# lune_engine: scene data, file formats and rendering. No Qt Widgets.
# ---------------------------------------------------------------------------
add_library(lune_engine STATIC
    src/engine/scene/scene.cpp
    src/engine/scene/components.cpp
    src/engine/scene/primitives.cpp
    src/engine/scene/picking.cpp
    src/engine/material/nodegraph.cpp
    src/engine/material/nodelibrary.cpp
    src/engine/material/materialcompiler.cpp
    src/engine/material/gltfmaterialgraph.cpp
    src/engine/io/objloader.cpp
    src/engine/io/gltfloader.cpp
    src/engine/io/meshimport.cpp
    src/engine/io/hdrimage.cpp
    src/engine/io/sceneio.cpp
    src/engine/io/imageloader.cpp
    src/engine/render/camera.cpp
    src/engine/render/renderer.cpp
    src/engine/render/texturecache.cpp
    src/engine/render/iblmaps.cpp
    src/engine/render/shadowmaps.cpp
)
# Includes are relative to src/: "engine/scene/scene.h", "frontend/core/selection.h"
target_include_directories(lune_engine PUBLIC src)
# QRhi headers (<rhi/qrhi.h>) are semi-public and ship with the GuiPrivate target
# ShaderTools: node materials compile mesh.frag again at runtime (QShaderBaker)
target_link_libraries(lune_engine PUBLIC Qt6::Gui Qt6::GuiPrivate PRIVATE cgltf Qt6::ShaderToolsPrivate)

# ---------------------------------------------------------------------------
# lune_editor_core: editor logic that doesn't need widgets (selection, undo
# commands, gizmo math), so the tests can use it without a UI.
# ---------------------------------------------------------------------------
add_library(lune_editor_core STATIC
    src/frontend/core/selection.cpp
    src/frontend/core/commands.cpp
    src/frontend/core/displaysettings.cpp
    src/frontend/core/displayhistory.cpp
    src/frontend/components/viewport/gizmo.cpp
    src/frontend/components/viewport/overlaydraw.cpp
    src/frontend/components/viewport/lighticons.cpp
)
target_link_libraries(lune_editor_core PUBLIC lune_engine)

# ---------------------------------------------------------------------------
# Lune: the editor application
# ---------------------------------------------------------------------------
add_executable(Lune
    src/frontend/components/viewport/viewport.cpp
    src/frontend/components/viewport/statsoverlay.cpp
    src/frontend/components/viewport/displaymenu.cpp
    src/frontend/components/viewport/cameramenu.cpp
    src/frontend/components/widgets/draglabel.cpp
    src/frontend/components/widgets/vector3.cpp
    src/frontend/components/panel/panel.cpp
    src/frontend/components/panel/inspector/inspector.cpp
    src/frontend/components/panel/assets/assets.cpp
    src/frontend/components/panel/nodes/nodeeditor.cpp
    src/frontend/components/panel/nodes/nodespanel.cpp
    src/frontend/components/panel/outliner/outliner.cpp
    src/frontend/components/palette/commandpalette.cpp
    src/frontend/editor.cpp
    src/frontend/frameanimationdriver.cpp
    src/frontend/theme/theme.cpp
    src/main.cpp
)
target_link_libraries(Lune PRIVATE lune_editor_core Qt6::Widgets)

# Shaders: compiled at build time by qsb (Qt Shader Tools) for every QRhi backend,
# embedded as resources at :/shaders/<name>.qsb. They're attached to the executable
# (not to the static engine library) so the resources are always linked in.
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
        src/engine/render/shaders/display.frag
        src/engine/render/shaders/downsample.frag
        src/engine/render/shaders/blur.frag
        src/engine/render/shaders/composite.frag
        src/engine/render/shaders/mesh.vert
        src/engine/render/shaders/mesh.frag
        src/engine/render/shaders/outline.vert
        src/engine/render/shaders/outline.frag
        src/engine/render/shaders/overlay.vert
        src/engine/render/shaders/overlay.frag
        src/engine/render/shaders/cube.vert
        src/engine/render/shaders/equirect_to_cube.frag
        src/engine/render/shaders/irradiance.frag
        src/engine/render/shaders/prefilter.frag
        src/engine/render/shaders/brdf_lut.frag
        src/engine/render/shaders/background.vert
        src/engine/render/shaders/background.frag
        src/engine/render/shaders/shadow.vert
        src/engine/render/shaders/shadow.frag
        src/engine/render/shaders/shadow_material.vert
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

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(LUNE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# GLSL sources needed at runtime: node materials put generated code into mesh.frag and
# compile it again (and, for their shadows, into shadow_material.glsl); the node types are
# read from material_nodes.glsl.
qt6_add_resources(Lune "shadersrc"
    PREFIX "/shadersrc"
    BASE "src/engine/render/shaders"
    FILES
        src/engine/render/shaders/mesh.frag
        src/engine/render/shaders/material_nodes.glsl
        src/engine/render/shaders/shadow_material.glsl
)
