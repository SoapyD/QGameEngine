@echo off
setlocal

REM Add MSYS2 to PATH
set PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%

if not exist "build" mkdir "build"
cd build

REM Use Ninja if available (faster), otherwise MinGW Makefiles
where ninja >nul 2>nul
if %ERRORLEVEL% == 0 (
    echo Using Ninja generator...
    cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Debug ..
) else (
    echo Using MinGW Makefiles generator...
    cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
)

if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

echo.
echo Configuration complete. To build, run:
echo   cd build ^&^& cmake --build .
pause
