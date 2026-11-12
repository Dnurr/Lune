@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Compila e lancia tutti i test automatici (tests\tst_*.cpp).
rem Argomenti extra vanno a ctest, es.:  test.bat -R sceneio   (solo i test che contengono "sceneio")
call scripts\build.bat || goto :fail

echo.
echo === Test ===
ctest --test-dir build --output-on-failure %*
if errorlevel 1 (
    echo.
    echo [ERRORE] Alcuni test sono falliti.
    goto :fail
)
echo.
echo Tutti i test sono passati.
exit /b 0

:fail
pause
exit /b 1
