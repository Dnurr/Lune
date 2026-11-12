@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem ---------------------------------------------------------------------------
rem 1) Ambiente MSVC: cl.exe non e' nel PATH di un prompt normale.
rem    Se non c'e', cerchiamo Visual Studio con vswhere e carichiamo vcvars64.bat
rem ---------------------------------------------------------------------------
where cl >nul 2>nul
if not errorlevel 1 goto :configure

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERRORE] vswhere.exe non trovato: Visual Studio non sembra installato.
    goto :fail
)

rem Preferisci VS 2022 (Qt e' compilato con msvc2022), altrimenti la versione piu' recente
set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -version [17.0^,18.0^) -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)
if not defined VSPATH (
    echo [ERRORE] Nessuna installazione di Visual Studio con i tool C++ MSVC x64.
    goto :fail
)

echo [INFO] Carico l'ambiente MSVC da "%VSPATH%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul
if errorlevel 1 (
    echo [ERRORE] vcvars64.bat eseguito ma cl.exe ancora non trovato.
    goto :fail
)

:configure
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
    goto :fail
)

echo.
echo === Build ===
cmake --build build
if errorlevel 1 (
    echo [ERRORE] Build fallita: il programma non verra' avviato.
    goto :fail
)

if not exist "build\Lune.exe" (
    echo [ERRORE] build\Lune.exe non trovato dopo la build.
    goto :fail
)

echo.
echo === Avvio Lune ===
"build\Lune.exe"
exit /b %errorlevel%

:fail
echo.
pause
exit /b 1
