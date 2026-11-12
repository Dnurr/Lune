@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Compila (configure + build, con l'ambiente MSVC) e avvia Lune solo se la build e' riuscita.
call scripts\build.bat || goto :fail

if not exist "build\Lune.exe" (
    echo [ERRORE] build\Lune.exe non trovato dopo la build.
    goto :fail
)

echo.
echo === Avvio Lune ===
"build\Lune.exe" %*
exit /b %errorlevel%

:fail
echo.
echo Il programma non verra' avviato.
pause
exit /b 1
