@echo off
setlocal enabledelayedexpansion
set BUILD_DIR=build
set EXE_NAME=specsviewer.exe
set DLL_PATH=libs\GLFW\lib-mingw-w64\glfw3.dll

rem --- гарантированно убить старый процесс, если висит
taskkill /f /im %EXE_NAME% >nul 2>&1

echo === Cleaning old build ===
if exist %BUILD_DIR% rd /s /q %BUILD_DIR%
mkdir %BUILD_DIR%

echo === Generating (CMake + MinGW) ===
@REM cmake -S . -B %BUILD_DIR% -G "MinGW Makefiles"
cmake -S . -B %BUILD_DIR% -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo === Building (clean-first) ===
cmake --build %BUILD_DIR% --clean-first -- -j

echo === Copying DLL ===
if exist "%~dp0%DLL_PATH%" (
    if not exist "%BUILD_DIR%\bin" mkdir "%BUILD_DIR%\bin"
    copy "%~dp0%DLL_PATH%" "%BUILD_DIR%\bin\" >nul
) else (
    echo ⚠️ DLL not found: %~dp0%DLL_PATH%
)

echo === Running program ===
echo Will run: "%~dp0%BUILD_DIR%\bin\%EXE_NAME%"
if exist "%BUILD_DIR%\bin\%EXE_NAME%" (
    start "" "%~dp0%BUILD_DIR%\bin\%EXE_NAME%"
) else (
    echo ❌ Build failed! %EXE_NAME% not found.
)

echo.
pause
endlocal
