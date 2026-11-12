# Troubleshooting

## `cl.exe` not found / "cl is not recognized"

The MSVC compiler is only on `PATH` inside a Visual Studio developer prompt. Use
`run.bat` / `test.bat`, which load the environment themselves
(`scripts/msvc-env.bat`), or open an *x64 Native Tools Command Prompt for VS 2022*.

`msvc-env.bat` looks for Visual Studio 2022 first (Qt `msvc2022_64` is built with it)
and falls back to the newest installation with the C++ x64 tools.

## CMake can't find Qt6

`CMAKE_PREFIX_PATH` in `CMakePresets.json` points to `C:/Qt/6.8.1/msvc2022_64`. Change
it if Qt is installed elsewhere, then delete `build/` and configure again.

## "Could not find a package configuration file provided by Qt6ShaderTools"

The Qt Shader Tools module is missing. Install it with the Qt Maintenance Tool, or:

```bat
python -m aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 -m qtshadertools --noarchives --outputdir C:\Qt
```

## Configure fails while downloading a dependency

Third-party libraries are fetched with Git on the first configure. Check the network
connection and that `git` is on `PATH`. Downloads are cached in `build/_deps`.

## "Cannot open include file" for a header that exists

Seen occasionally on this project with Qt or MSVC headers (`QSize`,
`__msvc_bit_utils.hpp`): the file is there and building again succeeds. It usually
means another process (typically real-time antivirus scanning) held the file while
several compilers ran in parallel. Run the build again; if it happens often, exclude
the `build/` folder from real-time scanning.

## The build is stuck on an old error after changing CMake files

If a previous configure failed halfway, `build/CMakeCache.txt` may exist without
`build/build.ninja`; `scripts/build.bat` detects this and starts from a clean `build/`.
For anything else, delete `build/` and run `run.bat` again.

## Tests can't find Qt DLLs

The test executables aren't deployed with `windeployqt`; CTest puts Qt's `bin` folder on
`PATH` for them. Run them through `test.bat` or `ctest`, not by double-clicking the
`.exe` (or add `C:\Qt\6.8.1\msvc2022_64\bin` to `PATH` first).

## The stats overlay shows 60 fps and nothing more

FPS is capped by vsync (the monitor refresh rate). 60 fps with a low CPU time means the
frame is far from the limit.
