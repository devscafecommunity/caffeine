@echo off
REM bin-manager.bat - Binary archival and restoration system for Windows
setlocal enabledelayedexpansion

set BIN_DIR=bin
set ARCHIVE_DIR=archives

if "%1"=="" goto usage
if "%1"=="list" goto cmd_list
if "%1"=="archive" goto cmd_archive
if "%1"=="restore" goto cmd_restore
if "%1"=="clean" goto cmd_clean
goto usage

:cmd_list
    if not exist "%BIN_DIR%" (
        echo [ERROR] Build directory %BIN_DIR% does not exist
        exit /b 1
    )
    
    echo [INFO] Binaries in %BIN_DIR%:
    dir /b /s "%BIN_DIR%\*.exe" "%BIN_DIR%\*.dll" 2>nul
    if errorlevel 1 echo [INFO] No binaries found
    exit /b 0

:cmd_archive
    set version=%2
    if "%version%"=="" (
        for /f "tokens=1-4 delims=/ " %%a in ('date /t') do set CDATE=%%c%%a%%b
        for /f "tokens=1-2 delims=: " %%a in ('time /t') do set CTIME=%%a%%b
        set version=!CDATE!-!CTIME!
    )
    
    if not exist "%BIN_DIR%" (
        echo [ERROR] Build directory %BIN_DIR% does not exist
        exit /b 1
    )
    
    if not exist "%ARCHIVE_DIR%" mkdir "%ARCHIVE_DIR%"
    set archive_file=%ARCHIVE_DIR%\caffeine-!version!.zip
    
    echo [INFO] Creating archive: !archive_file!
    powershell -Command "Compress-Archive -Path '%BIN_DIR%\*' -DestinationPath '!archive_file!' -Force" 2>nul
    if errorlevel 1 (
        echo [ERROR] Failed to create archive
        exit /b 1
    )
    
    echo [INFO] Archive created successfully: !archive_file!
    exit /b 0

:cmd_restore
    set version=%2
    if "%version%"=="" (
        echo [ERROR] Usage: %0 restore ^<version^>
        exit /b 1
    )
    
    set archive_file=%ARCHIVE_DIR%\caffeine-!version!.zip
    
    if not exist "!archive_file!" (
        echo [ERROR] Archive not found: !archive_file!
        exit /b 1
    )
    
    if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
    echo [INFO] Restoring from: !archive_file!
    powershell -Command "Expand-Archive -Path '!archive_file!' -DestinationPath '%BIN_DIR%' -Force" 2>nul
    if errorlevel 1 (
        echo [ERROR] Failed to restore archive
        exit /b 1
    )
    
    echo [INFO] Restore completed successfully
    exit /b 0

:cmd_clean
    if not exist "%BIN_DIR%" (
        echo [INFO] Build directory %BIN_DIR% does not exist, nothing to clean
        exit /b 0
    )
    
    echo [INFO] Cleaning binaries from %BIN_DIR%
    del /q /s "%BIN_DIR%\*" >nul 2>&1
    echo [INFO] Clean completed
    exit /b 0

:usage
    echo Usage: %0 {list^|archive [version]^|restore ^<version^>^|clean}
    exit /b 1
