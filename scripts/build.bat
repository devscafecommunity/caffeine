@echo off
REM Caffeine Engine Build Script for Windows
REM Usage: build.bat [debug|release] [clean] [test]

setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set BUILD_TYPE=Debug
set CLEAN_BUILD=0
set RUN_TESTS=0
set ACTION=build

REM Parse arguments
for %%A in (%*) do (
    if "%%A"=="release" set BUILD_TYPE=Release
    if "%%A"=="debug" set BUILD_TYPE=Debug
    if "%%A"=="clean" set CLEAN_BUILD=1
    if "%%A"=="test" set RUN_TESTS=1
    if "%%A"=="configure" set ACTION=configure
    if "%%A"=="rebuild" (
        set CLEAN_BUILD=1
        set ACTION=build
    )
)

echo.
echo ========================================
echo  Caffeine Engine - Build Manager
echo ========================================
echo.
echo Build Type:  %BUILD_TYPE%
echo Project:    %PROJECT_ROOT%
echo.

REM Check if Python is available
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python is not installed or not in PATH
    exit /b 1
)

REM Run Python build manager
cd /d %PROJECT_ROOT%

if %CLEAN_BUILD%==1 (
    echo [CLEAN] Removing build directory...
    python scripts/build_manager.py clean
)

echo [%BUILD_TYPE%] Starting build...
python scripts/build_manager.py configure --type %BUILD_TYPE%

if errorlevel 1 (
    echo ERROR: Configuration failed
    exit /b 1
)

python scripts/build_manager.py build --type %BUILD_TYPE%

if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

if %RUN_TESTS%==1 (
    echo [TEST] Running tests...
    python scripts/build_manager.py test
    if errorlevel 1 (
        echo WARNING: Some tests failed
    )
)

echo.
echo ========================================
echo  Build completed successfully!
echo ========================================
echo.

endlocal
