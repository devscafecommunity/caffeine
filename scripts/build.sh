#!/usr/bin/env bash

#######################################
# Unix Build Script for Caffeine
# Purpose: CMake build automation with colored output and parallel builds
# Usage: ./build.sh [debug|release|clean|test|rebuild]
#######################################

set -e  # Exit on error

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# Auto-detect project root (script is in scripts/ subdirectory)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

log_info "Project root: $PROJECT_ROOT"

# Detect CPU cores for parallel builds
if command -v nproc &> /dev/null; then
    CPU_CORES=$(nproc)
elif command -v sysctl &> /dev/null; then
    CPU_CORES=$(sysctl -n hw.ncpu)
else
    CPU_CORES=4
    log_warn "Could not detect CPU cores, defaulting to $CPU_CORES"
fi

log_info "Using $CPU_CORES CPU cores for parallel build"

# Check dependencies
check_dependencies() {
    log_info "Checking dependencies..."
    
    if ! command -v cmake &> /dev/null; then
        log_error "CMake is not installed. Please install CMake 3.10 or higher."
        exit 1
    fi
    
    if ! command -v python3 &> /dev/null; then
        log_error "Python 3 is not installed. Please install Python 3.6 or higher."
        exit 1
    fi
    
    log_success "All dependencies are installed"
}

# Clean build directory
clean_build() {
    log_info "Cleaning build directory..."
    if [ -d "$PROJECT_ROOT/build" ]; then
        rm -rf "$PROJECT_ROOT/build"
        log_success "Build directory cleaned"
    else
        log_warn "Build directory does not exist, nothing to clean"
    fi
}

# Configure CMake
configure_cmake() {
    local BUILD_TYPE=$1
    
    log_info "Configuring CMake (Build Type: $BUILD_TYPE)..."
    
    mkdir -p "$PROJECT_ROOT/build"
    cd "$PROJECT_ROOT/build"
    
    if cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" ..; then
        log_success "CMake configuration successful"
    else
        log_error "CMake configuration failed"
        exit 1
    fi
}

# Build project
build_project() {
    log_info "Building project with $CPU_CORES parallel jobs..."
    
    cd "$PROJECT_ROOT/build"
    
    if cmake --build . --parallel "$CPU_CORES"; then
        log_success "Build successful"
    else
        log_error "Build failed"
        exit 1
    fi
}

# Run tests
run_tests() {
    log_info "Running tests..."
    
    cd "$PROJECT_ROOT/build"
    
    if ctest --output-on-failure; then
        log_success "All tests passed"
    else
        log_error "Tests failed"
        exit 1
    fi
}

# Main script logic
main() {
    local BUILD_TYPE="Debug"
    local RUN_TESTS=false
    local CLEAN_FIRST=false
    
    # Parse arguments
    if [ $# -eq 0 ]; then
        log_info "No arguments provided, using default: Debug build"
    else
        case "$1" in
            debug)
                BUILD_TYPE="Debug"
                log_info "Building in Debug mode"
                ;;
            release)
                BUILD_TYPE="Release"
                log_info "Building in Release mode"
                ;;
            clean)
                check_dependencies
                clean_build
                exit 0
                ;;
            test)
                BUILD_TYPE="Debug"
                RUN_TESTS=true
                log_info "Building in Debug mode with tests"
                ;;
            rebuild)
                BUILD_TYPE="Debug"
                CLEAN_FIRST=true
                log_info "Rebuilding from scratch (Debug mode)"
                ;;
            *)
                log_error "Unknown argument: $1"
                echo "Usage: $0 [debug|release|clean|test|rebuild]"
                exit 1
                ;;
        esac
    fi
    
    # Execute build steps
    check_dependencies
    
    if [ "$CLEAN_FIRST" = true ]; then
        clean_build
    fi
    
    configure_cmake "$BUILD_TYPE"
    build_project
    
    if [ "$RUN_TESTS" = true ]; then
        run_tests
    fi
    
    log_success "Build process completed successfully"
}

# Run main function
main "$@"
