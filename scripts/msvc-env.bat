@echo off
rem ---------------------------------------------------------------------------
rem Carica l'ambiente MSVC x64 (cl.exe nel PATH) se non e' gia' attivo.
rem Uso:  call scripts\msvc-env.bat || goto :fail
rem Niente setlocal qui: le variabili devono restare nel prompt di chi chiama.
rem ---------------------------------------------------------------------------
where cl >nul 2>nul
if not errorlevel 1 exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERRORE] vswhere.exe non trovato: Visual Studio non sembra installato.
    exit /b 1
)

rem Preferisci VS 2022 (Qt e' compilato con msvc2022), altrimenti la versione piu' recente
set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -version [17.0^,18.0^) -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)
if not defined VSPATH (
    echo [ERRORE] Nessuna installazione di Visual Studio con i tool C++ MSVC x64.
    exit /b 1
)

echo [INFO] Carico l'ambiente MSVC da "%VSPATH%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul
if errorlevel 1 (
    echo [ERRORE] vcvars64.bat eseguito ma cl.exe ancora non trovato.
    exit /b 1
)
exit /b 0
