#!/usr/bin/env fish
# Caffeine Engine Build Script for Fish Shell
# Usage: build.fish [debug|release] [clean] [test] [configure|rebuild]

# Color codes for Fish
set -l RED '\033[0;31m'
set -l GREEN '\033[0;32m'
set -l BLUE '\033[0;34m'
set -l YELLOW '\033[1;33m'
set -l NC '\033[0m' # No Color

# Helper functions for colored output
function log_info
    echo -e "$BLUE[INFO]$NC $argv"
end

function log_success
    echo -e "$GREEN[SUCCESS]$NC $argv"
end

function log_error
    echo -e "$RED[ERROR]$NC $argv"
end

function log_warn
    echo -e "$YELLOW[WARN]$NC $argv"
end

# Find project root by looking for CMakeLists.txt
function find_project_root
    set -l current_dir (pwd)
    set -l search_dir (dirname (status --current-filename))
    
    # Try from script directory
    for i in (seq 0 5)
        if test -f "$search_dir/CMakeLists.txt"
            echo "$search_dir"
            return 0
        end
        set search_dir (dirname "$search_dir")
    end
    
    # Fallback to current directory
    echo "$current_dir"
end

# Initialize variables
set -l PROJECT_ROOT (find_project_root)
set -l BUILD_TYPE "Debug"
set -l CLEAN_BUILD 0
set -l RUN_TESTS 0
set -l ACTION "build"

# Parse command line arguments
for arg in $argv
    switch $arg
        case "release"
            set BUILD_TYPE "Release"
        case "debug"
            set BUILD_TYPE "Debug"
        case "clean"
            set CLEAN_BUILD 1
        case "test"
            set RUN_TESTS 1
        case "configure"
            set ACTION "configure"
        case "rebuild"
            set CLEAN_BUILD 1
            set ACTION "build"
        case "*"
            log_warn "Unknown argument: $arg"
    end
end

# Display build info
echo ""
echo "========================================"
echo " Caffeine Engine - Build Manager"
echo "========================================"
echo ""
echo "Build Type:  $BUILD_TYPE"
echo "Project:     $PROJECT_ROOT"
echo ""

# Check dependencies
if not command -v python3 >/dev/null 2>&1
    if not command -v python >/dev/null 2>&1
        log_error "Python is not installed or not in PATH"
        exit 1
    else
        set -l PYTHON_CMD python
    end
else
    set -l PYTHON_CMD python3
end

if not command -v cmake >/dev/null 2>&1
    log_error "CMake is not installed or not in PATH"
    exit 1
end

# Change to project root
cd "$PROJECT_ROOT"
or begin
    log_error "Failed to change to project directory: $PROJECT_ROOT"
    exit 1
end

# Clean build if requested
if test $CLEAN_BUILD -eq 1
    log_info "Removing build directory..."
    eval $PYTHON_CMD scripts/build_manager.py clean
    or begin
        log_error "Clean failed"
        exit 1
    end
end

# Configure step
log_info "[$BUILD_TYPE] Configuring project..."
eval $PYTHON_CMD scripts/build_manager.py configure --type $BUILD_TYPE
or begin
    log_error "Configuration failed"
    exit 1
end

# Build step (skip if action is configure only)
if test "$ACTION" != "configure"
    log_info "[$BUILD_TYPE] Building project..."
    eval $PYTHON_CMD scripts/build_manager.py build --type $BUILD_TYPE
    or begin
        log_error "Build failed"
        exit 1
    end
end

# Run tests if requested
if test $RUN_TESTS -eq 1
    log_info "Running tests..."
    eval $PYTHON_CMD scripts/build_manager.py test
    or begin
        log_warn "Some tests failed"
    end
end

# Success message
echo ""
echo "========================================"
echo " Build completed successfully!"
echo "========================================"
echo ""

exit 0
