# ast26

**CLI and Python library for Farming Simulator 2026 AST (GS2D v8) texture files.**

Read, write and inspect the `.ast` texture container used by
*Farming Simulator 2026* — with built-in ASTC encoding/decoding via
[astcenc](https://github.com/ARM-software/astc-encoder).

> Based on the CLI conventions of
> [gstextconv](https://github.com/snowbit64/gstextconv) by SnowBit64.

---

## Installation

```bash
pip install .
```

Requires **Python ≥ 3.10** and **Pillow ≥ 10**.

For ASTC encoding/decoding the
[astcenc](https://github.com/ARM-software/astc-encoder) CLI must be on
`PATH`. The tool will attempt to auto-install it if it is missing.

---

## CLI Usage

```
ast26 [global flags] <command> [options]
```

Global flags:

| Flag | Description |
| :--- | :--- |
| `-V`, `--version` | Print version |
| `-i`, `--info` | Print build metadata and capabilities |

Subcommands: [`encoder`](#encoder), [`decoder`](#decoder),
[`inspect`](#inspect).

### `encoder`

Convert `.png` / `.jpg` → `.ast` container.

```bash
# single file
ast26 encoder -f texture.png -o texture.ast

# batch: every PNG in a folder, overwrite enabled
ast26 encoder -d ./in -u ./out -O

# encode with max mipmaps and 6x6 blocks
ast26 encoder -f texture.png -o texture.ast -m max -k 6x6
```

| Flag | Description |
| :--- | :--- |
| `-f`, `-b`, `-d`, `-r` | Inputs (file / batch / directory / recursive) |
| `-k`, `--block-size <NxM>` | ASTC block size (`4x4` … `12x12`) |
| `-q`, `--quality <fast\|medium\|thorough>` | astcenc preset |
| `-s`, `--color-space <srgb\|linear\|alpha>` | Color space |
| `-t`, `--texture-type <2d\|2darray>` | Texture type |
| `-n`, `--ideal-origin <topLeft\|bottomLeft>` | Stored Y-axis origin |
| `-m`, `--num-mipmaps <max\|N>` | Number of additional mip levels |
| `-o`, `-u`, `-O` | Output file / directory / overwrite |
| `-x`, `--delete-source-file` | Delete source after a successful write |
| `-v`, `--verbose` | Per-file report |

### `decoder`

Convert `.ast` → `.png` / `.jpg` / `.astc` / `raw-rgba`.

```bash
# decode to PNG
ast26 decoder -f texture.ast -o texture.png

# decode all layers
ast26 decoder -f cubemap.ast -l -u ./out -O

# recursive batch decode
ast26 decoder -d ./in -r -u ./png -O
```

| Flag | Description |
| :--- | :--- |
| `-F`, `--format <png\|jpg\|astc\|raw-rgba>` | Output format (default: png) |
| `-i`, `--mip-index <n>` | Specific mip level (default 0) |
| `-m`, `--all-mips` | Extract every mip level |
| `-L`, `--layer-index <n>` | Specific layer (default 0) |
| `-l`, `--all-layers` | Extract every array layer |
| `-P`, `--pattern <tpl>` | Filename template for multi-output |
| `-g`, `--real-origin` | Preserve stored orientation (no auto-flip) |
| `-p`, `-x`, `-v` | Same semantics as in `encoder` |

### `inspect`

Print container metadata as JSON.

```bash
ast26 inspect -f texture.ast -a
ast26 inspect -d ./in -r -a
```

| Flag | Field |
| :--- | :--- |
| `-m`, `--num-mipmaps` | Number of mipmaps |
| `-l`, `--num-layers` | Number of layers |
| `-c`, `--compression` | Compression format |
| `-s`, `--size` | Base width and height |
| `--ideal-origin` | Stored ideal origin |
| `-S`, `--color-space` | Color space |
| `-n`, `--channels` | Number of channels |
| `-a`, `--all` | All fields above |

### Verbose output

Passing `-v` / `--verbose` to any subcommand emits one record per file:

```
ast26:
    index: 1;
    filename: in/texture.ast;
    new file: in/texture.png;
    process duration: 28.085ms;
    process type: decoding;
    status: success;
```

---

## Python API

```python
import ast26

# inspect
with open("texture.ast", "rb") as f:
    data = f.read()

info = ast26.inspect_file(data)
print(info.compression, info.num_mipmaps, info.num_layers)

# decode
img = ast26.decode(data)
print(img.width, img.height, img.num_layers, img.num_mipmaps)
layer0_mip0 = img.layers[0][0]  # MipLevel with .data (RGBA8 bytes)

# encode
png_bytes = open("texture.png", "rb").read()
ast_data = ast26.encode(
    png_bytes,
    block_size=(6, 6),
    quality="medium",
    color_space="srgb",
    num_mipmaps=5,
)
open("texture.ast", "wb").write(ast_data)
```

---

## Supported Formats

### GS2D Container

| Container | Version | Support |
| :--- | :--- | :--- |
| GS2D / FS26 | `v8` | read + write |

### ASTC block sizes

`4x4`, `5x4`, `5x5`, `6x5`, `6x6`, `8x5`, `8x6`, `8x8`, `10x5`, `10x6`,
`10x8`, `10x10`, `12x10`, `12x12`.

---

## Repository Layout

```
ast26/                   Python package
  __init__.py            Public API
  container.py           GS2D v8 header parsing and writing
  codec.py               Decode / encode / inspect logic
  astc.py                ASTC compression bridge (via astcenc CLI)
  cli.py                 CLI entry point
samples/                 FS26 reference .ast textures
pyproject.toml           Build configuration
```

---

## License

MIT — see [LICENSE](LICENSE).
