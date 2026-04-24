# Caffeine Build Scripts

Cross-platform build automation for the Caffeine Engine.

## Quick Start

### Windows (PowerShell/cmd)
```batch
scripts\build.bat release
scripts\build.bat debug test
scripts\build.bat clean
```

### Linux/macOS (Bash)
```bash
./scripts/build.sh release
./scripts/build.sh debug test
./scripts/build.sh clean
```

### Fish Shell
```fish
./scripts/build.fish release
./scripts/build.fish debug test
./scripts/build.fish clean
```

## Available Scripts

### build.* (Configure & Build)
Build the Caffeine engine with CMake.

**Commands:**
- `build [debug|release]` - Build in specified mode (default: debug)
- `build clean` - Clean build directory
- `build rebuild` - Clean + rebuild
- `build test` - Build and run tests

**Examples:**
```bash
./build.sh release           # Release build
./build.sh debug test        # Debug build with tests
./build.sh clean rebuild     # Clean rebuild
```

### bin-manager.* (Binary Archival)
Manage built binaries and create snapshots.

**Commands:**
- `bin-manager list` - List all built binaries
- `bin-manager archive [version]` - Archive current build
- `bin-manager restore <version>` - Restore archived build
- `bin-manager clean` - Clean binary directory

**Examples:**
```bash
./bin-manager.sh archive 1.0.0        # Archive with version
./bin-manager.sh list                 # List current binaries
./bin-manager.sh restore 1.0.0        # Restore specific version
```

### version_manager.py (Version Tracking)
Track and manage build versions with semantic versioning.

**Commands:**
- `show [--type Debug|Release]` - Show version info
- `bump [--type Debug|Release] [--component major|minor|patch]` - Increment version
- `export [--type Debug|Release] --output <file>` - Generate C++ header

**Examples:**
```bash
python3 version_manager.py show --type Release
python3 version_manager.py bump --type Debug --component minor
python3 version_manager.py export --type Release --output src/Version.hpp
```

## Directory Structure

```
scripts/
├── README.md               # This file
├── build.bat              # Windows build script
├── build.sh               # Unix build script
├── build.fish             # Fish shell script
├── bin-manager.bat        # Windows binary manager
├── bin-manager.sh         # Unix binary manager
├── build_manager.py       # Python build core
├── version_manager.py     # Version tracking
└── __init__.py           # Package marker
```

## Environment

### Requirements
- Python 3.8+
- CMake 3.20+
- Git
- C++20 compiler

### Verified On
- Windows 10/11 (cmd, PowerShell)
- macOS 11+ (bash, zsh, fish)
- Linux (Debian/Ubuntu, bash, fish)

## Tips

1. **Parallel builds**: Scripts automatically detect CPU cores
2. **Clean rebuilds**: Use `build.sh clean rebuild` for full recompilation
3. **Binary snapshots**: Archive before major changes with `bin-manager archive`
4. **Version tracking**: Use `version_manager.py` for CI/CD integration

## Troubleshooting

### CMake not found
```bash
# Install CMake
brew install cmake          # macOS
apt-get install cmake       # Linux
```

### Python not found
```bash
# Install Python 3
brew install python3        # macOS
apt-get install python3     # Linux
```

### Permission denied (Unix)
```bash
chmod +x scripts/*.sh scripts/*.fish
```

## Development

To modify scripts, follow these patterns:

- All error handling uses exit codes (0=success)
- Colored output for readability
- Cross-platform compatibility
- Python scripts delegate to `build_manager.py`
