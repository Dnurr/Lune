# Third-party libraries, downloaded at configure time (into build/_deps) with FetchContent.
# Every dependency is pinned to a release tag, so builds are reproducible.
#
# To add one: FetchContent_Declare(<name> ...) + FetchContent_MakeAvailable(<name>), then
# expose it as a target and link it in CMakeLists.txt.
#
# Planned (added when the work that needs them starts, they are heavy to build):
#   OpenEXR  - EXR image sequences (render output)
#   OpenColorIO - color management

include(FetchContent)

# cgltf: glTF 2.0 loader, single header (MIT). https://github.com/jkuhlmann/cgltf
# SOURCE_SUBDIR points to a folder without CMakeLists: only download, don't build their tests.
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.15
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-build
)
FetchContent_MakeAvailable(cgltf)

add_library(cgltf INTERFACE)
target_include_directories(cgltf SYSTEM INTERFACE "${cgltf_SOURCE_DIR}")
