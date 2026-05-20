# SPDX-License-Identifier: MIT
"""Codec layer — decode, encode, and inspect FS26 AST (GS2D v8) files."""

from __future__ import annotations

import io
import json
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

from PIL import Image as PILImage

from ast26.container import (
    ASTC_BLOCKS,
    HEADER_SIZE,
    Header,
    astc_chain_size,
    astc_mip_size,
    compute_content_hash,
    infer_astc_block,
    parse_header,
    write_header,
)
from ast26.astc import decompress_astc_block, compress_astc_image

# ---------------------------------------------------------------------------
# Decoded image representation
# ---------------------------------------------------------------------------


@dataclass
class MipLevel:
    """Raw pixel data for one mip level of one layer."""

    width: int
    height: int
    data: bytes  # RGBA8 pixels


@dataclass
class DecodedImage:
    """Fully decoded texture with all layers and mip levels."""

    width: int
    height: int
    num_layers: int = 1
    num_mipmaps: int = 1
    channels: int = 4
    block_size: tuple[int, int] = (6, 6)
    is_astc: bool = True
    flip_mode: int = 1
    header: Optional[Header] = None
    layers: List[List[MipLevel]] = field(default_factory=list)


@dataclass
class InspectResult:
    """Metadata for a single AST file."""

    container_version: int = 8
    texture_type: str = "2d"
    width: int = 0
    height: int = 0
    depth: int = 1
    channels: int = 4
    num_mipmaps: int = 1
    num_layers: int = 1
    compression: str = "astc_6x6"
    block_size: Optional[tuple[int, int]] = None
    color_space: str = "srgb"
    ideal_origin: str = "bottomLeft"
    format_code_a: int = 0x22
    format_code_b: int = 1


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------


def decode(data: bytes) -> DecodedImage:
    """Decode a GS2D v8 AST file into an :class:`DecodedImage`."""
    hdr = parse_header(data)
    payload = data[HEADER_SIZE:]
    n_mips = hdr.mip_count + 1

    result = DecodedImage(
        width=hdr.dim_x,
        height=hdr.dim_y,
        channels=hdr.channels,
        flip_mode=hdr.flip_mode,
        header=hdr,
    )

    # Try ASTC compressed
    inferred = infer_astc_block(hdr.dim_x, hdr.dim_y, len(payload), n_mips)
    if inferred is not None:
        (bx, by), layers = inferred
        result.block_size = (bx, by)
        result.is_astc = True
        result.num_layers = layers
        result.num_mipmaps = n_mips

        chain = astc_chain_size(hdr.dim_x, hdr.dim_y, bx, by, n_mips)
        for layer_idx in range(layers):
            layer_mips: List[MipLevel] = []
            off = layer_idx * chain
            for mip in range(n_mips):
                mw = max(1, hdr.dim_x >> mip)
                mh = max(1, hdr.dim_y >> mip)
                ms = astc_mip_size(mw, mh, bx, by)
                astc_data = payload[off : off + ms]
                rgba = decompress_astc_block(astc_data, mw, mh, bx, by)
                layer_mips.append(MipLevel(mw, mh, rgba))
                off += ms
            result.layers.append(layer_mips)
        return result

    # Try uncompressed raw (single or multi-layer)
    for bpp in (1, 2, 3, 4, 8, 16):
        chain = 0
        for m in range(n_mips):
            mw = max(1, hdr.dim_x >> m)
            mh = max(1, hdr.dim_y >> m)
            chain += mw * mh * bpp
        if chain > 0 and len(payload) % chain == 0:
            layers = len(payload) // chain
            if 1 <= layers <= 256:
                result.is_astc = False
                result.block_size = (0, 0)
                result.num_mipmaps = n_mips
                result.num_layers = layers
                for layer_idx in range(layers):
                    layer_mips: List[MipLevel] = []
                    off = layer_idx * chain
                    for mip in range(n_mips):
                        mw = max(1, hdr.dim_x >> mip)
                        mh = max(1, hdr.dim_y >> mip)
                        ms = mw * mh * bpp
                        raw = payload[off : off + ms]
                        rgba = _raw_to_rgba8(raw, mw, mh, bpp, hdr.channels)
                        layer_mips.append(MipLevel(mw, mh, rgba))
                        off += ms
                    result.layers.append(layer_mips)
                return result

    raise ValueError(
        f"cannot decode payload ({len(payload)} bytes) for "
        f"{hdr.dim_x}x{hdr.dim_y} with {n_mips} mip(s)"
    )


def encode(
    image_data: bytes,
    *,
    block_size: tuple[int, int] = (6, 6),
    quality: str = "medium",
    color_space: str = "srgb",
    num_mipmaps: int = 0,
    flip_mode: int = 1,
    texture_type: str = "2d",
    channels: int | None = None,
    images: list[bytes] | None = None,
) -> bytes:
    """Encode a PNG/RGBA image into a GS2D v8 AST container.

    Parameters
    ----------
    image_data : bytes
        PNG file bytes (or raw RGBA8 when width/height are known).
    block_size : tuple[int, int]
        ASTC block size, e.g. ``(6, 6)``.
    quality : str
        One of ``"fast"``, ``"medium"``, ``"thorough"``.
    color_space : str
        One of ``"srgb"``, ``"linear"``, ``"alpha"``.
    num_mipmaps : int
        Additional mip levels (0 = base only, negative = auto max).
    flip_mode : int
        0 = topLeft, 1 = bottomLeft.
    texture_type : str
        ``"2d"`` or ``"2darray"``.
    channels : int | None
        Override channel count (1, 3, 4). Auto-detected if *None*.
    images : list[bytes] | None
        Extra images for 2darray encoding.
    """
    bx, by = block_size
    if (bx, by) not in ASTC_BLOCKS:
        raise ValueError(f"unsupported ASTC block size {bx}x{by}")

    all_images = [image_data]
    if images:
        all_images.extend(images)

    all_payloads: list[bytes] = []
    width = height = 0
    detected_channels = 4

    for idx, img_bytes in enumerate(all_images):
        pil = PILImage.open(io.BytesIO(img_bytes))
        if idx == 0:
            width, height = pil.size
            if pil.mode == "L":
                detected_channels = 1
            elif pil.mode == "RGB":
                detected_channels = 3
            else:
                detected_channels = 4

        pil = pil.convert("RGBA")
        rgba = pil.tobytes()

        if num_mipmaps < 0:
            total_mips = _max_mip_count(width, height)
        else:
            total_mips = num_mipmaps + 1

        layer_payload = bytearray()
        cur_w, cur_h = width, height
        cur_rgba = rgba
        for mip in range(total_mips):
            if mip > 0:
                cur_w = max(1, width >> mip)
                cur_h = max(1, height >> mip)
                mip_pil = pil.resize((cur_w, cur_h), PILImage.Resampling.LANCZOS)
                cur_rgba = mip_pil.tobytes()
            astc_data = compress_astc_image(
                cur_rgba, cur_w, cur_h, bx, by, quality, color_space
            )
            layer_payload.extend(astc_data)

        all_payloads.append(bytes(layer_payload))

    payload = b"".join(all_payloads)

    if channels is None:
        channels = detected_channels
    if num_mipmaps < 0:
        actual_mip_count = _max_mip_count(width, height) - 1
    else:
        actual_mip_count = num_mipmaps

    tt = 1
    fc_b = 1
    if texture_type == "2darray":
        tt = 2
        fc_b = 6

    h = Header(
        dim_x=width,
        dim_y=height,
        dim_z=1,
        channels=channels,
        mip_count=actual_mip_count,
        texture_type=tt,
        flags=4,
        content_hash=compute_content_hash(payload),
        format_code_a=0x22,
        flip_mode=flip_mode,
        format_code_b=fc_b,
        tail_byte=payload[0] if payload else 0,
    )
    return write_header(h) + payload


def inspect_file(data: bytes) -> InspectResult:
    """Parse metadata from a GS2D v8 file without full decompression."""
    hdr = parse_header(data)
    payload = data[HEADER_SIZE:]
    n_mips = hdr.mip_count + 1

    result = InspectResult(
        container_version=hdr.version,
        width=hdr.dim_x,
        height=hdr.dim_y,
        depth=hdr.dim_z,
        channels=hdr.channels,
        num_mipmaps=n_mips,
        format_code_a=hdr.format_code_a,
        format_code_b=hdr.format_code_b,
    )

    # Texture type
    tt_map = {1: "2d", 2: "2darray", 3: "cube", 4: "cubearray", 50: "special"}
    result.texture_type = tt_map.get(hdr.texture_type, f"unknown({hdr.texture_type})")

    # Flip / origin
    result.ideal_origin = "topLeft" if hdr.flip_mode == 0 else "bottomLeft"

    # ASTC inference
    inferred = infer_astc_block(hdr.dim_x, hdr.dim_y, len(payload), n_mips)
    if inferred is not None:
        (bx, by), layers = inferred
        result.compression = f"astc_{bx}x{by}"
        result.block_size = (bx, by)
        result.num_layers = layers
    else:
        found = False
        for bpp in (1, 2, 3, 4, 8, 16):
            chain = sum(
                max(1, hdr.dim_x >> m) * max(1, hdr.dim_y >> m) * bpp
                for m in range(n_mips)
            )
            if chain > 0 and len(payload) % chain == 0:
                layers = len(payload) // chain
                if 1 <= layers <= 256:
                    result.compression = f"uncompressed_{bpp}bpp"
                    result.block_size = None
                    result.num_layers = layers
                    found = True
                    break
        if not found:
            result.compression = "unknown"
            result.block_size = None

    return result


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _raw_to_rgba8(raw: bytes, w: int, h: int, bpp: int, channels: int) -> bytes:
    """Convert raw pixel data to RGBA8."""
    if bpp == 4 and channels == 4:
        return raw
    pixels = w * h
    out = bytearray(pixels * 4)
    for i in range(pixels):
        src_off = i * bpp
        if channels == 1:
            v = raw[src_off] if src_off < len(raw) else 0
            out[i * 4] = v
            out[i * 4 + 1] = v
            out[i * 4 + 2] = v
            out[i * 4 + 3] = 255
        elif channels == 3:
            r = raw[src_off] if src_off < len(raw) else 0
            g = raw[src_off + 1] if src_off + 1 < len(raw) else 0
            b = raw[src_off + 2] if src_off + 2 < len(raw) else 0
            out[i * 4] = r
            out[i * 4 + 1] = g
            out[i * 4 + 2] = b
            out[i * 4 + 3] = 255
        else:
            for c in range(min(bpp, 4)):
                if src_off + c < len(raw):
                    out[i * 4 + c] = raw[src_off + c]
            if bpp < 4:
                out[i * 4 + 3] = 255
    return bytes(out)


def _max_mip_count(w: int, h: int) -> int:
    """Maximum number of mip levels including the base (down to 1x1)."""
    levels = 1
    while (w > 1 or h > 1) and levels < 16:
        w = max(1, w >> 1)
        h = max(1, h >> 1)
        levels += 1
    return levels
