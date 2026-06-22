@echo off
REM ════════════════════════════════════════════════════════════════════════════
REM Caffeine Engine - Universal Build Wrapper (Windows)
REM Purpose: Single easy-to-use entry point for all build operations
REM Usage: caffeine-build [command] [options]
REM
REM COMMANDS:
REM   config      Configure CMake
REM   build       Build project
REM   rebuild     Clean + configure + build
REM   test        Run test suite
REM   run         Execute binary
REM   clean       Remove build artifacts
REM   help        Show help
REM
REM EXAMPLES:
REM   caffeine-build                      # Debug build
REM   caffeine-build build --release      # Release build
REM   caffeine-build rebuild --release    # Full rebuild
REM   caffeine-build test                 # Run tests
REM   caffeine-build run                  # Execute game
REM ════════════════════════════════════════════════════════════════════════════

setlocal enabledelayedexpansion

REM Colors (if available)
set "RESET=[0m"
set "GREEN=[32m"
set "BLUE=[34m"
set "YELLOW=[33m"
set "RED=[31m"

REM Setup
set "PROJECT_ROOT=%~dp0"
set "BUILD_DIR=%PROJECT_ROOT%build"
set "BIN_DIR=%PROJECT_ROOT%bin"

REM Defaults
set "BUILD_TYPE=Debug"
set "HEADLESS=OFF"
set "SCRIPTING=OFF"
set "PARALLEL_JOBS=0"
set "CLEAN_FIRST=0"

REM Detect CPU cores (simplified for Windows)
for /f "tokens=2 delims==" %%i in ('wmic os get logicalprocessorcount /value') do set PARALLEL_JOBS=%%i
if "%PARALLEL_JOBS%"=="0" set PARALLEL_JOBS=4

echo [INFO] Project root: %PROJECT_ROOT%
echo [INFO] CPU cores: %PARALLEL_JOBS%

REM Parse arguments
set "COMMAND=build"
if not "%1"=="" set "COMMAND=%1"

:parse_args
if "%1"=="" goto args_done
if "%1"=="--debug" (set "BUILD_TYPE=Debug" & shift & goto parse_args)
if "%1"=="--release" (set "BUILD_TYPE=Release" & shift & goto parse_args)
if "%1"=="--headless" (set "HEADLESS=ON" & shift & goto parse_args)
if "%1"=="--scripting" (set "SCRIPTING=ON" & shift & goto parse_args)
if "%1"=="--clean" (set "CLEAN_FIRST=1" & shift & goto parse_args)
shift
goto parse_args

:args_done

REM Route to subcommand
if /i "%COMMAND%"=="config" goto cmd_config
if /i "%COMMAND%"=="build" goto cmd_build
if /i "%COMMAND%"=="rebuild" goto cmd_rebuild
if /i "%COMMAND%"=="test" goto cmd_test
if /i "%COMMAND%"=="run" goto cmd_run
if /i "%COMMAND%"=="clean" goto cmd_clean
if /i "%COMMAND%"=="help" goto cmd_help
goto cmd_help

:cmd_config
echo [INFO] Configuring CMake...
echo Build Type: %BUILD_TYPE%
echo Headless: %HEADLESS%
echo Scripting: %SCRIPTING%
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
cmake -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCAFFEINE_BUILD_HEADLESS=%HEADLESS% -DCAFFEINE_ENABLE_SCRIPTING=%SCRIPTING% "%PROJECT_ROOT%"
if errorlevel 1 echo [ERROR] Configuration failed & exit /b 1
echo [SUCCESS] Configuration complete
exit /b 0

:cmd_build
if "%CLEAN_FIRST%"=="1" goto cmd_clean
if not exist "%BUILD_DIR%\CMakeCache.txt" goto cmd_config
echo [INFO] Building project with %PARALLEL_JOBS% parallel jobs...
cd /d "%BUILD_DIR%"
cmake --build . --config %BUILD_TYPE% --parallel %PARALLEL_JOBS%
if errorlevel 1 echo [ERROR] Build failed & exit /b 1
echo [SUCCESS] Build complete
exit /b 0

:cmd_rebuild
call :cmd_clean
call :cmd_config
call :cmd_build
exit /b %errorlevel%

:cmd_test
if not exist "%BUILD_DIR%" (
    echo [ERROR] Build directory not found, run 'caffeine-build build' first
    exit /b 1
)
cd /d "%BUILD_DIR%"
if exist "ctest.exe" (
    ctest --output-on-failure --parallel %PARALLEL_JOBS%
) else (
    cmake --build . --target test
)
if errorlevel 1 echo [ERROR] Tests failed & exit /b 1
echo [SUCCESS] Tests passed
exit /b 0

:cmd_run
setlocal enabledelayedexpansion
set "EXE_PATH="
for /r "%BUILD_DIR%" %%f in (caffeine-combined.exe) do (
    set "EXE_PATH=%%f"
)
if not exist "!EXE_PATH!" (
    echo [ERROR] Executable not found
    exit /b 1
)
echo [INFO] Running: !EXE_PATH!
call !EXE_PATH!
exit /b %errorlevel%

:cmd_clean
echo [INFO] Cleaning build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    echo [SUCCESS] Build directory removed
)
if exist "%BIN_DIR%" (
    rmdir /s /q "%BIN_DIR%"
    echo [SUCCESS] Binary directory removed
)
exit /b 0

:cmd_help
echo.
echo ┌──────────────────────────────────────────────────────────────┐
echo │  Caffeine Engine - Build Wrapper (Windows)                   │
echo │  Easy-to-use build automation                                │
echo └──────────────────────────────────────────────────────────────┘
echo.
echo USAGE:
echo   caffeine-build [command] [options]
echo.
echo COMMANDS:
echo   config              Configure CMake
echo   build               Build project
echo   rebuild             Clean + configure + build
echo   test                Run test suite
echo   run                 Execute binary
echo   clean               Remove build artifacts
echo   help                Show this help
echo.
echo QUICK EXAMPLES:
echo   caffeine-build                      # Debug build
echo   caffeine-build build --release      # Release build
echo   caffeine-build rebuild --release    # Full rebuild
echo   caffeine-build test                 # Run tests
echo   caffeine-build run                  # Execute game
echo.
echo OPTIONS:
echo   --debug             Debug build (default)
echo   --release           Release build (optimized)
echo   --headless          Build without SDL3
echo   --scripting         Enable Lua scripting
echo   --clean             Clean before build
echo.
exit /b 0

endlocal
