@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Builds Lune and packs a self-contained Windows build into out\Lune-windows-x64.zip:
rem the executable, the Qt libraries and plugins it needs (windeployqt), the MSVC runtime
rem (app-local, so no installer is needed) and the demo scenes.

call scripts\build.bat || goto :fail

set "STAGE=out\Lune"
set "ZIP=out\Lune-windows-x64.zip"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%ZIP%" del /q "%ZIP%"
mkdir "%STAGE%" || goto :fail

echo.
echo === Deploy ===
copy /y "build\Lune.exe" "%STAGE%\" >nul || goto :fail
rem Qt's folder is the one the build was configured with (CMAKE_PREFIX_PATH in the cache)
set "QTDIR=C:/Qt/6.8.1/msvc2022_64"
for /f "tokens=2 delims==" %%a in ('findstr /b "CMAKE_PREFIX_PATH:" build\CMakeCache.txt') do set "QTDIR=%%a"
set "WINDEPLOYQT=%QTDIR:/=\%\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    echo [ERROR] windeployqt not found: "%WINDEPLOYQT%"
    goto :fail
)
"%WINDEPLOYQT%" --release --no-translations --no-opengl-sw --no-system-d3d-compiler ^
    --no-compiler-runtime "%STAGE%\Lune.exe" || goto :fail

rem MSVC runtime next to the executable (vcvars sets VCToolsRedistDir)
for /d %%d in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do copy /y "%%d\*.dll" "%STAGE%\" >nul
if not exist "%STAGE%\vcruntime140.dll" echo [WARNING] MSVC runtime not copied: the zip needs the VC++ redistributable installed.

xcopy /e /i /q /y "demos" "%STAGE%\demos" >nul || goto :fail
copy /y "LICENSE" "%STAGE%\LICENSE.txt" >nul
(
    echo Lune - real-time renderer and editor for visual effects
    echo.
    echo Run Lune.exe. To try it, open demos\materials.lune ^(File ^> Open, or drag it onto Lune.exe^).
    echo Requires Windows 10 or 11, 64-bit, with a Direct3D 11 capable GPU.
    echo.
    echo Source, documentation and shortcuts: https://github.com/Dnurr/Lune
) > "%STAGE%\README.txt"

echo.
echo === Zip ===
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%' -DestinationPath '%ZIP%' -Force" || goto :fail
echo.
echo [OK] %ZIP%
exit /b 0

:fail
echo.
echo Packaging failed.
exit /b 1
