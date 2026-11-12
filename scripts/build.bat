@echo off
rem ---------------------------------------------------------------------------
rem Configure + build (ambiente MSVC incluso). Usato da run.bat e test.bat.
rem Uso:  call scripts\build.bat || goto :fail
rem ---------------------------------------------------------------------------
call "%~dp0msvc-env.bat" || exit /b 1

rem Se una configurazione precedente e' fallita (cache senza build.ninja), riparti da zero
if exist "build\CMakeCache.txt" if not exist "build\build.ninja" (
    echo [INFO] Cache CMake incompleta, pulisco la cartella build...
    rmdir /s /q build
)

echo.
echo === Configure ===
cmake --preset default
if errorlevel 1 (
    echo [ERRORE] Configurazione CMake fallita.
    exit /b 1
)

echo.
echo === Build ===
cmake --build build
if errorlevel 1 (
    echo [ERRORE] Build fallita.
    exit /b 1
)
exit /b 0
