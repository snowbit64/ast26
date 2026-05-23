# GIMP Plug-in for ast26

GIMP 2.10 file-format plug-in that adds native support for Farming Simulator 2026 `.ast` (GS2D v8) textures.

## Features

- **Open** `.ast` files directly in GIMP (File → Open)
- **Export** images as `.ast` (File → Export As → `.ast`)
- Export dialog with configurable:
  - ASTC block size (4×4 … 12×12)
  - Compression quality (Fast / Medium / Thorough)
  - Mipmap generation (-1 = all levels)
  - Color space (sRGB / Linear / Alpha)
- Multi-layer textures imported as separate GIMP layers
- Original block size preserved via GIMP parasites and used as default on re-export

## Build

Requires `libgimp2.0-dev` (GIMP 2.10 development headers).

```bash
# Ubuntu / Debian
sudo apt-get install libgimp2.0-dev

# Build with GIMP plug-in enabled
cmake -B build -DCMAKE_BUILD_TYPE=Release -DAST26_BUILD_GIMP=ON
cmake --build build --config Release --parallel
```

## Install

### Pre-built binaries (recommended)

Download the latest artifact from the [Actions](../../actions) page:

- **Linux**: `ast26-gimp-plugin-linux-x64`
- **Windows**: `ast26-gimp-plugin-windows-x64`

#### Linux

```bash
chmod +x file-ast26
mkdir -p ~/.config/GIMP/2.10/plug-ins
cp file-ast26 ~/.config/GIMP/2.10/plug-ins/
```

#### Windows

1. Extract the downloaded zip (`file-ast26.exe` + DLLs)
2. Copy **all files** to your GIMP plug-ins folder:
   ```
   %APPDATA%\GIMP\2.10\plug-ins\
   ```
   Or typically: `C:\Users\<user>\AppData\Roaming\GIMP\2.10\plug-ins\`

3. Restart GIMP

### CMake install (Linux)

```bash
sudo cmake --install build
```

This places `file-ast26` in the system GIMP plug-ins directory detected by `gimptool-2.0`.

### Build from source (Windows — MSYS2)

```bash
# Inside MSYS2 MinGW64 shell
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-gimp pkg-config
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DAST26_BUILD_GIMP=ON
cmake --build build --parallel
cp build/file-ast26.exe /c/Users/$USER/AppData/Roaming/GIMP/2.10/plug-ins/
```

Restart GIMP after installing.

## Usage

Once installed, GIMP will automatically recognize `.ast` files:

- **Open**: File → Open → select any `.ast` file
- **Export**: File → Export As → choose "Farming Simulator 2026 AST" format or type a `.ast` extension

The export dialog lets you configure ASTC compression settings before saving.

## Requirements

- GIMP 2.10.x
- CMake ≥ 3.20
- C++17 compiler
- `pkg-config`
