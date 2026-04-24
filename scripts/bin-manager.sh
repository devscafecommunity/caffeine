#!/usr/bin/env bash
# bin-manager.sh - Binary archival and restoration system

set -e

BIN_DIR="bin"
ARCHIVE_DIR="archives"

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

cmd_list() {
    if [ ! -d "$BIN_DIR" ]; then
        log_error "Build directory $BIN_DIR does not exist"
        return 1
    fi
    
    log_info "Binaries in $BIN_DIR:"
    find "$BIN_DIR" -type f -executable -o -name "*.exe" -o -name "*.dll" -o -name "*.so" -o -name "*.dylib" 2>/dev/null || log_info "No binaries found"
}

cmd_archive() {
    local version="${1:-$(date +%Y%m%d-%H%M%S)}"
    
    if [ ! -d "$BIN_DIR" ]; then
        log_error "Build directory $BIN_DIR does not exist"
        return 1
    fi
    
    mkdir -p "$ARCHIVE_DIR"
    local archive_file="$ARCHIVE_DIR/caffeine-${version}.tar.gz"
    
    log_info "Creating archive: $archive_file"
    tar -czf "$archive_file" -C "$BIN_DIR" . 2>/dev/null || {
        log_error "Failed to create archive"
        return 1
    }
    
    log_info "Archive created successfully: $archive_file"
}

cmd_restore() {
    local version="$1"
    
    if [ -z "$version" ]; then
        log_error "Usage: $0 restore <version>"
        return 1
    fi
    
    local archive_file="$ARCHIVE_DIR/caffeine-${version}.tar.gz"
    
    if [ ! -f "$archive_file" ]; then
        log_error "Archive not found: $archive_file"
        return 1
    fi
    
    mkdir -p "$BIN_DIR"
    log_info "Restoring from: $archive_file"
    tar -xzf "$archive_file" -C "$BIN_DIR" || {
        log_error "Failed to restore archive"
        return 1
    }
    
    log_info "Restore completed successfully"
}

cmd_clean() {
    if [ ! -d "$BIN_DIR" ]; then
        log_info "Build directory $BIN_DIR does not exist, nothing to clean"
        return 0
    fi
    
    log_info "Cleaning binaries from $BIN_DIR"
    rm -rf "${BIN_DIR:?}"/*
    log_info "Clean completed"
}

case "${1:-}" in
    list)
        cmd_list
        ;;
    archive)
        cmd_archive "$2"
        ;;
    restore)
        cmd_restore "$2"
        ;;
    clean)
        cmd_clean
        ;;
    *)
        echo "Usage: $0 {list|archive [version]|restore <version>|clean}"
        exit 1
        ;;
esac
