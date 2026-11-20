@echo off
REM ==========================================================================
REM  SmartParking - build script for Windows 11 + MinGW (g++) + VS Code
REM  Run this from the project root:   build.bat
REM ==========================================================================
setlocal

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ was not found in PATH.
    echo Install MinGW-w64 ^(MSYS2 or WinLibs^) and add its bin folder to PATH.
    exit /b 1
)

echo [1/2] Compiling SQLite ...
gcc -O2 -c backend\third_party\sqlite3.c -o sqlite3.o
if errorlevel 1 goto :failed

echo [2/2] Compiling the SmartParking backend ...
g++ -std=c++17 -O2 -Wall -Wextra -Ibackend\third_party ^
    backend\main.cpp backend\parking.cpp backend\graph.cpp ^
    backend\booking.cpp backend\database.cpp backend\http.cpp ^
    sqlite3.o -o smartparking.exe -lws2_32
if errorlevel 1 goto :failed

echo.
echo Build complete:  smartparking.exe
echo Start the website:  smartparking.exe --port 8080
echo Then open:          http://localhost:8080
goto :eof

:failed
echo.
echo Build failed. Read the errors above.
exit /b 1
