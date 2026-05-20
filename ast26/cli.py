# SPDX-License-Identifier: MIT
"""CLI entry-point for *ast26* — mirrors the gstextconv flag conventions."""

from __future__ import annotations

import argparse
import io
import json
import os
import sys
import time
from pathlib import Path
from typing import List

from ast26 import __version__
from ast26.codec import decode, encode, inspect_file
from ast26.container import ASTC_BLOCKS


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

AST_EXTS = {".ast", ".gs2d"}
IMG_EXTS = {".png", ".jpg", ".jpeg", ".dds"}


def _collect_inputs(
    file: str | None,
    batch: list[str] | None,
    directory: str | None,
    recursive: bool,
    exts: set[str],
) -> List[Path]:
    """Gather input file paths from the various input flags."""
    paths: list[Path] = []
    if file:
        paths.append(Path(file))
    if batch:
        for b in batch:
            paths.append(Path(b))
    if directory:
        d = Path(directory)
        if recursive:
            for p in sorted(d.rglob("*")):
                if p.suffix.lower() in exts and p.is_file():
                    paths.append(p)
        else:
            for p in sorted(d.iterdir()):
                if p.suffix.lower() in exts and p.is_file():
                    paths.append(p)
    return paths


def _output_path(
    src: Path,
    output: str | None,
    output_dir: str | None,
    ext: str,
) -> Path:
    """Determine the output path for a single file."""
    if output:
        return Path(output)
    if output_dir:
        return Path(output_dir) / (src.stem + ext)
    return src.with_suffix(ext)


def _layer_output_path(
    src: Path,
    output_dir: str | None,
    layer: int,
    mip: int,
    ext: str,
    pattern: str | None = None,
) -> Path:
    """Build a per-layer/mip output path."""
    if pattern:
        name = (
            pattern
            .replace("{filename}", src.stem)
            .replace("{layerIndex}", str(layer))
            .replace("{mipIndex}", str(mip))
            .replace("{format}", ext.lstrip("."))
        )
    else:
        name = f"{src.stem}-layer{layer}-mip{mip}{ext}"
    if output_dir:
        return Path(output_dir) / name
    return src.parent / name


# ---------------------------------------------------------------------------
# Sub-commands
# ---------------------------------------------------------------------------

def cmd_decoder(args: argparse.Namespace) -> int:
    """Decode .ast → .png (or other format)."""
    paths = _collect_inputs(
        args.file, args.batch, args.dir, args.recursive, AST_EXTS
    )
    if not paths:
        print("error: no input files specified", file=sys.stderr)
        return 1

    fmt_ext = {"png": ".png", "jpg": ".jpg", "raw-rgba": ".rgba", "astc": ".astc"}
    out_ext = fmt_ext.get(args.format, ".png")

    ok = 0
    for idx, src in enumerate(paths, 1):
        t0 = time.monotonic()
        try:
            data = src.read_bytes()
            img = decode(data)
        except Exception as exc:
            print(f"error: {src}: {exc}", file=sys.stderr)
            continue

        layers_to_emit = list(range(img.num_layers))
        if not args.all_layers and args.layer_index is not None:
            layers_to_emit = [args.layer_index]
        elif not args.all_layers:
            layers_to_emit = [0]

        mips_to_emit = list(range(img.num_mipmaps))
        if not args.all_mips and args.mip_index is not None:
            mips_to_emit = [args.mip_index]
        elif not args.all_mips:
            mips_to_emit = [0]

        for li in layers_to_emit:
            if li >= len(img.layers):
                print(f"warning: {src}: layer {li} out of range", file=sys.stderr)
                continue
            for mi in mips_to_emit:
                if mi >= len(img.layers[li]):
                    print(f"warning: {src}: mip {mi} out of range", file=sys.stderr)
                    continue

                mip = img.layers[li][mi]

                if args.all_layers or args.all_mips:
                    out = _layer_output_path(
                        src, args.output_dir, li, mi, out_ext, args.pattern
                    )
                elif args.preserve:
                    out = src.with_suffix(out_ext)
                else:
                    out = _output_path(src, args.output, args.output_dir, out_ext)

                if out.exists() and not args.overwrite:
                    if args.verbose:
                        _verbose(idx, str(src), str(out), 0, "decoding", "skipped")
                    continue

                out.parent.mkdir(parents=True, exist_ok=True)

                if args.format in ("png", "jpg", "jpeg"):
                    from PIL import Image as PILImage
                    pil = PILImage.frombytes("RGBA", (mip.width, mip.height), mip.data)
                    if img.flip_mode == 0 and not args.real_origin:
                        pil = pil.transpose(PILImage.Transpose.FLIP_TOP_BOTTOM)
                    pil.save(out)
                elif args.format == "raw-rgba":
                    out.write_bytes(mip.data)
                elif args.format == "astc":
                    # re-extract raw ASTC blocks from the source
                    hdr = img.header
                    assert hdr is not None
                    payload = data[0x34:]
                    out.write_bytes(payload)

        dur = (time.monotonic() - t0) * 1000
        if args.verbose:
            _verbose(idx, str(src), str(out), dur, "decoding", "success")
        ok += 1

        if args.delete_source:
            src.unlink(missing_ok=True)

    return 0 if ok > 0 else 1


def cmd_encoder(args: argparse.Namespace) -> int:
    """Encode .png / .jpg → .ast."""
    paths = _collect_inputs(
        args.file, args.batch, args.dir, args.recursive, IMG_EXTS
    )
    if not paths:
        print("error: no input files specified", file=sys.stderr)
        return 1

    bx, by = _parse_block_size(args.block_size)

    ok = 0
    for idx, src in enumerate(paths, 1):
        t0 = time.monotonic()
        try:
            img_bytes = src.read_bytes()
            out_path = _output_path(src, args.output, args.output_dir, ".ast")
            if out_path.exists() and not args.overwrite:
                if args.verbose:
                    _verbose(idx, str(src), str(out_path), 0, "encoding", "skipped")
                continue

            out_path.parent.mkdir(parents=True, exist_ok=True)

            flip = 0 if args.ideal_origin == "topLeft" else 1
            mips = -1 if args.num_mipmaps == "max" else int(args.num_mipmaps)
            ch = None
            if args.channels:
                ch = len(args.channels)

            ast_data = encode(
                img_bytes,
                block_size=(bx, by),
                quality=args.quality,
                color_space=args.color_space,
                num_mipmaps=mips,
                flip_mode=flip,
                texture_type=args.texture_type,
                channels=ch,
            )
            out_path.write_bytes(ast_data)
        except Exception as exc:
            print(f"error: {src}: {exc}", file=sys.stderr)
            continue

        dur = (time.monotonic() - t0) * 1000
        if args.verbose:
            _verbose(idx, str(src), str(out_path), dur, "encoding", "success")
        ok += 1

        if args.delete_source:
            src.unlink(missing_ok=True)

    return 0 if ok > 0 else 1


def cmd_inspect(args: argparse.Namespace) -> int:
    """Print texture metadata as JSON."""
    exts = AST_EXTS | IMG_EXTS
    paths = _collect_inputs(args.file, args.batch, args.dir, args.recursive, exts)
    if not paths:
        print("error: no input files specified", file=sys.stderr)
        return 1

    results = []
    for src in paths:
        try:
            data = src.read_bytes()
            info = inspect_file(data)
        except Exception as exc:
            results.append({"file": str(src), "error": str(exc)})
            continue

        entry: dict = {"file": str(src)}

        # Select requested fields (default all)
        show_all = args.all or not any([
            args.num_mipmaps, args.num_layers, args.compression,
            args.size, args.ideal_origin, args.color_space_field,
            args.channels_field,
        ])

        entry["container_version"] = info.container_version
        entry["texture_type"] = info.texture_type

        if show_all or args.size:
            entry["size"] = {"width": info.width, "height": info.height}
        if show_all or args.compression:
            entry["compression"] = info.compression
        if show_all or args.num_mipmaps:
            entry["num_mipmaps"] = info.num_mipmaps
        if show_all or args.num_layers:
            entry["num_layers"] = info.num_layers
        if show_all or args.ideal_origin:
            entry["ideal_origin"] = info.ideal_origin
        if show_all or args.color_space_field:
            entry["color_space"] = info.color_space
        if show_all or args.channels_field:
            entry["channels"] = info.channels

        results.append(entry)

    output = json.dumps(results, indent=2)
    if args.output:
        Path(args.output).write_text(output + "\n")
    else:
        print(output)

    return 0


# ---------------------------------------------------------------------------
# Verbose report (mirrors gstextconv format)
# ---------------------------------------------------------------------------

def _verbose(
    index: int, src: str, dst: str, dur_ms: float, ptype: str, status: str
) -> None:
    print(
        f"ast26:\n"
        f"    index: {index};\n"
        f"    filename: {src};\n"
        f"    new file: {dst};\n"
        f"    process duration: {dur_ms:.3f}ms;\n"
        f"    process type: {ptype};\n"
        f"    status: {status};"
    )


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

def _parse_block_size(s: str) -> tuple[int, int]:
    """Parse ``"NxM"`` block-size string."""
    parts = s.lower().split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError(f"invalid block size: {s}")
    bx, by = int(parts[0]), int(parts[1])
    if (bx, by) not in ASTC_BLOCKS:
        valid = ", ".join(f"{a}x{b}" for a, b in ASTC_BLOCKS)
        raise argparse.ArgumentTypeError(
            f"unsupported block size {bx}x{by}. Valid: {valid}"
        )
    return bx, by


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="ast26",
        description="Texture converter for Farming Simulator 2026 AST (GS2D v8) files",
    )
    parser.add_argument(
        "-V", "--version", action="version", version=f"%(prog)s {__version__}"
    )
    parser.add_argument(
        "-i", "--info", action="store_true", help="print build metadata"
    )

    sub = parser.add_subparsers(dest="command")

    # ---- encoder ----
    enc = sub.add_parser("encoder", help="encode images into AST containers")
    enc.add_argument("-f", "--file", help="single source image")
    enc.add_argument("-b", "--batch", action="append", help="add source (repeatable)")
    enc.add_argument("-d", "--dir", help="directory of source images")
    enc.add_argument("-r", "--recursive", action="store_true")
    enc.add_argument("-m", "--num-mipmaps", default="0",
                     help="additional mip levels (or 'max')")
    enc.add_argument("-k", "--block-size", default="6x6",
                     help="ASTC block size (default: 6x6)")
    enc.add_argument("-q", "--quality", default="medium",
                     choices=["fast", "medium", "thorough"])
    enc.add_argument("-s", "--color-space", default="srgb",
                     choices=["srgb", "linear", "alpha"])
    enc.add_argument("-c", "--channels", default=None,
                     help="image channels (r, g, b, a)")
    enc.add_argument("-t", "--texture-type", default="2d",
                     choices=["2d", "2darray"])
    enc.add_argument("-n", "--ideal-origin", default="bottomLeft",
                     choices=["topLeft", "bottomLeft"])
    enc.add_argument("-o", "--output", help="output file path")
    enc.add_argument("-u", "--output-dir", help="output directory")
    enc.add_argument("-O", "--overwrite", action="store_true")
    enc.add_argument("-x", "--delete-source-file", dest="delete_source",
                     action="store_true")
    enc.add_argument("-v", "--verbose", action="store_true")

    # ---- decoder ----
    dec = sub.add_parser("decoder", help="decode AST containers into images")
    dec.add_argument("-f", "--file", help="single AST source file")
    dec.add_argument("-b", "--batch", action="append", help="add source (repeatable)")
    dec.add_argument("-d", "--dir", help="directory of AST files")
    dec.add_argument("-r", "--recursive", action="store_true")
    dec.add_argument("-F", "--format", default="png",
                     choices=["png", "jpg", "astc", "raw-rgba"])
    dec.add_argument("-c", "--channels", default=None, help="channel swizzle")
    dec.add_argument("-i", "--mip-index", type=int, default=None)
    dec.add_argument("-m", "--all-mips", action="store_true")
    dec.add_argument("-L", "--layer-index", type=int, default=None)
    dec.add_argument("-l", "--all-layers", action="store_true")
    dec.add_argument("-P", "--pattern", default=None,
                     help="filename template for multi-output")
    dec.add_argument("-g", "--real-origin", action="store_true",
                     help="keep original orientation")
    dec.add_argument("-o", "--output", help="output file path")
    dec.add_argument("-u", "--output-dir", help="output directory")
    dec.add_argument("-O", "--overwrite", action="store_true")
    dec.add_argument("-p", "--preserve-file-path", dest="preserve",
                     action="store_true")
    dec.add_argument("-x", "--delete-source-file", dest="delete_source",
                     action="store_true")
    dec.add_argument("-v", "--verbose", action="store_true")

    # ---- inspect ----
    ins = sub.add_parser("inspect", help="print texture metadata as JSON")
    ins.add_argument("-f", "--file", help="single source file")
    ins.add_argument("-b", "--batch", action="append", help="add source (repeatable)")
    ins.add_argument("-d", "--dir", help="directory of files")
    ins.add_argument("-r", "--recursive", action="store_true")
    ins.add_argument("-m", "--num-mipmaps", action="store_true")
    ins.add_argument("-l", "--num-layers", action="store_true")
    ins.add_argument("-c", "--compression", action="store_true")
    ins.add_argument("-s", "--size", action="store_true")
    ins.add_argument("--ideal-origin", dest="ideal_origin", action="store_true")
    ins.add_argument("-S", "--color-space", dest="color_space_field",
                     action="store_true")
    ins.add_argument("-n", "--channels", dest="channels_field", action="store_true")
    ins.add_argument("-a", "--all", action="store_true")
    ins.add_argument("-o", "--output", help="output JSON file")
    ins.add_argument("-v", "--verbose", action="store_true")

    return parser


def _print_info() -> None:
    print(
        f"build release: {__version__};\n"
        f"tool name: ast26;\n"
        f"tool description: texture converter for Farming Simulator 2026 AST (GS2D v8);\n"
        f"supported textures versions: v8;\n"
        f"supported formats input: ast, png, jpg;\n"
        f"supported formats output: png, jpg, raw-rgba, astc;\n"
        f"cli support: yes;\n"
        f"batch conversion: yes;\n"
        f"recursive folders: yes;\n"
        f"mipmap support: yes;\n"
        f"alpha channel support: yes;\n"
        f"compression backend: astcenc;\n"
    )


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    if args.info:
        _print_info()
        return

    if args.command == "encoder":
        sys.exit(cmd_encoder(args))
    elif args.command == "decoder":
        sys.exit(cmd_decoder(args))
    elif args.command == "inspect":
        sys.exit(cmd_inspect(args))
    else:
        parser.print_help()
        sys.exit(0)
