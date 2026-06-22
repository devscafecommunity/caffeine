# ☕ Caffeine Engine - Universal Build Script

**Easy-to-use unified build automation for all platforms.**

One command to configure, build, test, and run the Caffeine Engine on Windows, macOS, and Linux.

---

## Quick Start

### Linux/macOS (Bash)
```bash
./caffeine-build                    # Debug build
./caffeine-build build --release    # Release build
./caffeine-build rebuild --release  # Full rebuild
./caffeine-build test               # Run tests
./caffeine-build run                # Execute game
```

### Windows (PowerShell/CMD)
```batch
.\caffeine-build                    # Debug build
.\caffeine-build build --release    # Release build
.\caffeine-build rebuild --release  # Full rebuild
.\caffeine-build test               # Run tests
.\caffeine-build run                # Execute game
```

---

## Commands

### `build` - Configure & Compile
Builds the project with automatic configuration if needed.

```bash
./caffeine-build build              # Build Debug
./caffeine-build build --release    # Build Release
./caffeine-build build --release --jobs 8   # Custom parallel jobs
```

**What it does:**
1. Checks CMake configuration (runs `config` if not configured)
2. Compiles with parallel jobs (auto-detected CPU cores)
3. Lists built artifacts

**Exit codes:**
- `0` = Success
- `1` = Build failed

---

### `config` - Configure CMake
Sets up CMake configuration without building.

```bash
./caffeine-build config                      # Debug config
./caffeine-build config --release            # Release config
./caffeine-build config --headless           # Headless (no graphics)
./caffeine-build config --scripting          # Enable Lua scripting
./caffeine-build config --release --headless # Combined options
```

**Options:**
- `--debug` - Debug mode with symbols (default)
- `--release` - Release mode with optimizations
- `--headless` - Build without SDL3/graphics (server/CI mode)
- `--scripting` - Enable Lua scripting support

---

### `rebuild` - Clean + Configure + Build
Full rebuild from scratch.

```bash
./caffeine-build rebuild                     # Full Debug rebuild
./caffeine-build rebuild --release           # Full Release rebuild
./caffeine-build rebuild --release --headless # Full Release headless rebuild
```

**What it does:**
1. Removes all build artifacts (`build/` and `bin/`)
2. Runs CMake configuration
3. Builds project

**Use this when:**
- You've changed CMakeLists.txt
- Build is broken and needs fresh start
- You're switching between Debug/Release

---

### `test` - Run Test Suite
Executes all unit tests.

```bash
./caffeine-build test               # Run tests (auto-builds if needed)
./caffeine-build build && ./caffeine-build test  # Build then test
```

**What it does:**
1. Verifies build directory exists
2. Runs CTest with parallel execution
3. Reports success/failure

**Exit codes:**
- `0` = All tests passed
- `1` = One or more tests failed

---

### `run` - Execute Binary
Launches the built game/editor executable.

```bash
./caffeine-build run                # Run Release binary if exists, else Debug
```

**What it does:**
1. Searches for built executable in standard locations
2. Launches with same working directory

**Searches:**
- `./bin/caffeine-combined`
- `./build/apps/doppio/caffeine-combined`
- `./build/bin/caffeine-combined`
- Full `./build/` recursion as fallback

---

### `clean` - Remove Build Artifacts
Deletes all build outputs.

```bash
./caffeine-build clean              # Remove build/ and bin/
```

**Removes:**
- `./build/` - CMake build directory
- `./bin/` - Binary directory

**Use this when:**
- You want to free disk space
- Build is corrupted
- Switching major build options

---

### `help` - Show Help
Displays command reference and examples.

```bash
./caffeine-build help
./caffeine-build --help
./caffeine-build -h
```

---

## Global Options

These options can be combined with any command:

```bash
./caffeine-build [command] [options]
```

### `--debug` (Default)
Build in debug mode with full symbols.

```bash
./caffeine-build build --debug
./caffeine-build rebuild --debug
```

### `--release`
Build in release mode with optimizations.

```bash
./caffeine-build build --release
./caffeine-build rebuild --release
```

### `--headless`
Build without SDL3/graphics (server/CI mode).

```bash
./caffeine-build config --headless
./caffeine-build rebuild --headless
```

### `--scripting`
Enable Lua scripting support.

```bash
./caffeine-build config --scripting
./caffeine-build build --scripting
```

### `--clean`
Clean build artifacts before building.

```bash
./caffeine-build build --clean              # Same as: clean && build
./caffeine-build build --release --clean    # Release rebuild
```

### `--jobs N`
Use N parallel compilation jobs.

```bash
./caffeine-build build --jobs 8     # Use 8 cores
./caffeine-build rebuild --jobs 4   # Use 4 cores
```

**Default:** Auto-detected from CPU count

---

## Common Workflows

### Fresh Start (Recommended First Build)
```bash
./caffeine-build rebuild
```

### Debug Development Loop
```bash
./caffeine-build build && ./caffeine-build test
```

### Release Build
```bash
./caffeine-build rebuild --release
```

### Run Game After Build
```bash
./caffeine-build build && ./caffeine-build run
```

### Full Development Cycle
```bash
./caffeine-build clean          # Start fresh
./caffeine-build build          # Build Debug
./caffeine-build test           # Run tests
./caffeine-build run            # Execute game
```

### Server/CI Build (No Graphics)
```bash
./caffeine-build rebuild --headless --release
```

### Quick Iteration
```bash
./caffeine-build build && ./caffeine-build test && ./caffeine-build run
```

---

## Directory Structure

```
caffeine/
├── caffeine-build           # Unix/macOS build script (executable)
├── caffeine-build.bat       # Windows build script
├── build/                   # CMake build directory (created)
│   ├── CMakeCache.txt
│   ├── Makefile
│   ├── compile_commands.json
│   └── tests/
├── bin/                     # Built binaries (created)
│   └── caffeine-combined    # Main executable
├── src/                     # Source code
├── tests/                   # Test suite
├── docs/
│   └── building.md          # Detailed build documentation
└── scripts/
    ├── README.md            # Legacy scripts reference
    ├── build.sh             # Original shell script
    ├── build_manager.py     # Build manager library
    └── version_manager.py   # Version tracking
```

---

## Requirements

### All Platforms
- **CMake** 3.20+ ([install](https://cmake.org/download/))
- **Git** ([install](https://git-scm.com/))
- **C++ Compiler** supporting C++20

### Platform-Specific

**Linux/macOS:**
- `bash` shell
- `python3` (optional, for extra features)
- `ctest` (part of CMake)

**Windows:**
- PowerShell 5.0+ OR Command Prompt
- Visual Studio 2022 (with C++ workload) OR
- MSVC compiler via command line

### Optional Dependencies
- **SDL3** - For graphics (disable with `--headless`)
- **Lua 5.3+** - For scripting (enable with `--scripting`)
- **ImGui** - For editor UI (included)

---

## Installation

### 1. Make Script Executable (Linux/macOS only)
```bash
chmod +x caffeine-build
```

### 2. Optional: Add to PATH
To use from anywhere:

**Linux/macOS:**
```bash
# Copy to global location
sudo cp caffeine-build /usr/local/bin/

# Then use from anywhere:
caffeine-build build --release
```

**Windows:**
```batch
REM Add project directory to PATH environment variable
REM Or run from project root: .\caffeine-build
```

---

## Troubleshooting

### "CMake not found"
Install CMake:
```bash
# macOS
brew install cmake

# Linux
apt-get install cmake

# Windows
choco install cmake
# OR download from https://cmake.org/download/
```

### "Permission denied" (Linux/macOS)
Make script executable:
```bash
chmod +x caffeine-build
```

### Build fails with "SDL3 not found"
Either:
1. Install SDL3: `brew install sdl3` (macOS)
2. Use headless mode: `./caffeine-build rebuild --headless`

### "Python not found"
Some build features require Python 3:
```bash
# macOS
brew install python3

# Linux
apt-get install python3
```

### Clean rebuild needed
```bash
./caffeine-build clean rebuild
```

### Parallel jobs slow down build
Reduce jobs:
```bash
./caffeine-build build --jobs 2
```

---

## Performance Tips

1. **Auto-detect cores:** Script auto-detects CPU cores (use `--jobs N` to override)
2. **Incremental builds:** Don't use `clean` unless necessary
3. **ccache (Optional):** Speeds up recompilation
   ```bash
   # Linux/macOS
   brew install ccache
   ```
4. **Precompiled headers:** CMake uses them automatically
5. **Parallel linking:** Build with `--jobs $(nproc)` for full parallelism

---

## Advanced Usage

### Build Specific Target
Edit `caffeine-build` to add:
```bash
cmake --build . --target caffeine-core --parallel $PARALLEL_JOBS
```

### Install Build
Add to script:
```bash
cmake --install build --config Release --prefix /usr/local
```

### Export Compile Commands
For IDE integration:
```bash
./caffeine-build config
# Compile commands in: build/compile_commands.json
```

### Profile Build Time
```bash
time ./caffeine-build rebuild --release
```

### Verbose Output
```bash
cd build
cmake --build . --verbose --parallel 1
```

---

## Legacy Scripts

The original scripts in `scripts/` are still available:

- `scripts/build.sh` - Original shell script
- `scripts/build.bat` - Original batch script
- `scripts/build_manager.py` - Python build manager

The new `caffeine-build` script is recommended for daily use.

---

## Contributing

To improve the build script:

1. Test changes on all three platforms (Windows, macOS, Linux)
2. Ensure backwards compatibility
3. Update this documentation
4. Follow existing shell conventions

---

## References

- [CMake Documentation](https://cmake.org/cmake/help/latest/)
- [CTest Documentation](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
- [C++20 Compiler Support](https://en.cppreference.com/w/cpp/compiler_support)
- See also: `docs/building.md`

---

**Last Updated:** May 2026  
**Maintained By:** Caffeine Development Team
