# ast26

Texture converter for **Farming Simulator 2026** AST files (GS2D v8 container).

Standalone C++ CLI with embedded [astcenc](https://github.com/ARM-software/astc-encoder) for ASTC compression/decompression, [bcdec](https://github.com/iOrange/bcdec) for BCn/DDS block decoding, and [stb](https://github.com/nothings/stb) for PNG/JPG I/O.

Based on the [gstextconv](https://github.com/snowbit64/gstextconv) project by SnowBit64.

## Features

- **Decode** `.ast` (GS2D v8) → PNG / JPG / ASTC / raw-RGBA
- **Encode** PNG / JPG / DDS → `.ast` (GS2D v8) with configurable ASTC block size
- **Inspect** AST file metadata (JSON output)
- DDS input support (BC1/BC3/BC4/BC5/BC7, uncompressed RGBA/BGRA/RGB, DX10 arrays)
- Multi-layer (2DArray, cubemap) support
- Full mipmap chain generation
- Standalone static binary — no runtime dependencies
- **GIMP plug-in** — open and export `.ast` files directly in GIMP 2.10 or 3.0 (see [gimp-plugin/](gimp-plugin/README.md))

## Supported ASTC Block Sizes

4x4, 5x4, 5x5, 6x5, 6x6, 8x5, 8x6, 8x8, 10x5, 10x6, 10x8, 10x10, 12x10, 12x12

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The binary is at `build/ast26` (Linux) or `build/Release/ast26.exe` (Windows).

### Build with GIMP 2.10 plug-in

```bash
sudo apt-get install libgimp2.0-dev          # Ubuntu / Debian
cmake -B build -DCMAKE_BUILD_TYPE=Release -DAST26_BUILD_GIMP=ON
cmake --build build --config Release --parallel
```

Install the plug-in into GIMP's user directory:

```bash
mkdir -p ~/.config/GIMP/2.10/plug-ins
cp build/file-ast26 ~/.config/GIMP/2.10/plug-ins/
```

### Build with GIMP 3.0 plug-in

```bash
sudo apt-get install libgimp-3.0-dev          # Ubuntu 25.04+ / Debian
cmake -B build -DCMAKE_BUILD_TYPE=Release -DAST26_BUILD_GIMP3=ON
cmake --build build --config Release --parallel
```

Install the plug-in into GIMP 3.0's user directory (must be in its own subdirectory):

```bash
mkdir -p ~/.config/GIMP/3.0/plug-ins/file-ast26
cp build/file-ast26 ~/.config/GIMP/3.0/plug-ins/file-ast26/
```

### Cross-compile for Android aarch64

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Usage

### Inspect

```bash
ast26 inspect -f texture.ast -a
ast26 inspect -d textures/ -r -a
```

### Decode

```bash
ast26 decoder -f texture.ast -o output.png
ast26 decoder -f texture.ast -F jpg -o output.jpg
ast26 decoder -f texture.ast -l -u ./layers/ -O          # all layers
ast26 decoder -f texture.ast -m -u ./mips/ -O             # all mipmaps
ast26 decoder -d textures/ -r -u ./output/ -O -v          # batch decode
```

### Encode

```bash
ast26 encoder -f input.png -o output.ast -k 6x6 -q medium
ast26 encoder -f input.dds -o output.ast -k 4x4 -m max
ast26 encoder -d images/ -r -u ./output/ -O -v            # batch encode
```

### CLI Flags

Follows [gstextconv](https://github.com/snowbit64/gstextconv) flag conventions:

| Flag | Description |
|------|-------------|
| `-f` | Single input file |
| `-b` | Batch input (repeatable) |
| `-d` | Directory input |
| `-r` | Recursive directory walk |
| `-k` | ASTC block size (e.g. `6x6`) |
| `-q` | Quality (`fast`, `medium`, `thorough`) |
| `-s` | Color space (`srgb`, `linear`, `alpha`) |
| `-m` | Mipmaps (encoder: count/`max`; decoder: all mips) |
| `-F` | Output format (`png`, `jpg`, `astc`, `raw-rgba`) |
| `-l` | All layers |
| `-o` | Output file path |
| `-u` | Output directory |
| `-O` | Overwrite existing |
| `-v` | Verbose output |

## License

MIT — see [LICENSE](LICENSE).

Third-party components: astcenc (Apache-2.0), bcdec (MIT), miniz (MIT), stb (MIT/public domain).
