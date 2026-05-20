# SPDX-License-Identifier: MIT
"""GS2D v8 container header parser and writer for Farming Simulator 2026."""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass, field
from typing import List

MAGIC = b"GS2D"
VERSION = 8
HEADER_SIZE = 0x34  # 52 bytes

# ASTC block shapes (same order as gstextconv / astcenc spec).
ASTC_BLOCKS: List[tuple[int, int]] = [
    (4, 4), (5, 4), (5, 5), (6, 5), (6, 6),
    (8, 5), (8, 6), (8, 8),
    (10, 5), (10, 6), (10, 8), (10, 10),
    (12, 10), (12, 12),
]

# Flags — kept identical to gstextconv naming.
FLAG_COMPRESSED = 0x01
FLAG_HAS_MIPMAPS = 0x02
FLAG_HAS_AUX = 0x04


@dataclass
class Header:
    """Parsed GS2D v8 header (52 bytes)."""

    version: int = VERSION
    dim_x: int = 0
    dim_y: int = 0
    dim_z: int = 1
    channels: int = 4
    mip_count: int = 0          # additional mip levels (total = mip_count + 1)
    texture_type: int = 1       # 1=2D, 2=2Darray, 3=cube?, 50=special
    reserved_17: int = 0
    flags: int = 4              # u32 at 0x18
    content_hash: bytes = field(default_factory=lambda: b"\x00" * 16)
    format_code_a: int = 0x22   # 0x22=ASTC, 0x05=mixed, 0x01=raw
    reserved_2d: int = 0
    flip_mode: int = 1          # 0=topLeft, 1=bottomLeft
    format_code_b: int = 1      # 1=default, 5=cubemap, 6=array
    padding: bytes = field(default_factory=lambda: b"\x00" * 3)
    tail_byte: int = 0          # byte at 0x33 — purpose unknown, preserved


def _rd_u8(data: bytes, off: int) -> int:
    return data[off]


def _rd_u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def _rd_u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def is_gs2d(data: bytes) -> bool:
    return len(data) >= 8 and data[:4] == MAGIC


def version_of(data: bytes) -> int:
    if not is_gs2d(data):
        raise ValueError("not a GS2D container")
    return _rd_u32(data, 4)


def parse_header(data: bytes) -> Header:
    """Parse a 52-byte GS2D v8 header from *data*."""
    if len(data) < HEADER_SIZE:
        raise ValueError(f"data too short for header ({len(data)} < {HEADER_SIZE})")
    if data[:4] != MAGIC:
        raise ValueError("invalid GS2D magic")
    ver = _rd_u32(data, 0x04)
    if ver != VERSION:
        raise ValueError(f"unsupported GS2D version {ver} (expected {VERSION})")

    h = Header()
    h.version = ver
    h.dim_x = _rd_u32(data, 0x08)
    h.dim_y = _rd_u32(data, 0x0C)
    h.dim_z = _rd_u32(data, 0x10)
    h.channels = _rd_u8(data, 0x14)
    h.mip_count = _rd_u8(data, 0x15)
    h.texture_type = _rd_u8(data, 0x16)
    h.reserved_17 = _rd_u8(data, 0x17)
    h.flags = _rd_u32(data, 0x18)
    h.content_hash = data[0x1C:0x2C]
    h.format_code_a = _rd_u8(data, 0x2C)
    h.reserved_2d = _rd_u8(data, 0x2D)
    h.flip_mode = _rd_u8(data, 0x2E)
    h.format_code_b = _rd_u8(data, 0x2F)
    h.padding = data[0x30:0x33]
    h.tail_byte = _rd_u8(data, 0x33)
    return h


def write_header(h: Header) -> bytes:
    """Serialize a Header back to 52 bytes."""
    buf = bytearray(HEADER_SIZE)
    buf[0:4] = MAGIC
    struct.pack_into("<I", buf, 0x04, h.version)
    struct.pack_into("<I", buf, 0x08, h.dim_x)
    struct.pack_into("<I", buf, 0x0C, h.dim_y)
    struct.pack_into("<I", buf, 0x10, h.dim_z)
    buf[0x14] = h.channels
    buf[0x15] = h.mip_count
    buf[0x16] = h.texture_type
    buf[0x17] = h.reserved_17
    struct.pack_into("<I", buf, 0x18, h.flags)
    buf[0x1C:0x2C] = h.content_hash[:16]
    buf[0x2C] = h.format_code_a
    buf[0x2D] = h.reserved_2d
    buf[0x2E] = h.flip_mode
    buf[0x2F] = h.format_code_b
    buf[0x30:0x33] = h.padding[:3]
    buf[0x33] = h.tail_byte
    return bytes(buf)


# ---------------------------------------------------------------------------
# ASTC helpers
# ---------------------------------------------------------------------------

def _ceil_div(a: int, b: int) -> int:
    return (a + b - 1) // b


def astc_mip_size(w: int, h: int, bx: int, by: int) -> int:
    """Size in bytes of a single mip level in ASTC block format."""
    return _ceil_div(w, bx) * _ceil_div(h, by) * 16


def astc_chain_size(w: int, h: int, bx: int, by: int, n_mips: int) -> int:
    """Total bytes for a full mip chain of *n_mips* levels."""
    total = 0
    for m in range(n_mips):
        mw = max(1, w >> m)
        mh = max(1, h >> m)
        total += astc_mip_size(mw, mh, bx, by)
    return total


def infer_astc_block(
    dim_x: int, dim_y: int, payload_size: int, n_mips: int
) -> tuple[tuple[int, int], int] | None:
    """Infer ASTC block size and layer count from the payload size.

    Returns ``((bx, by), layers)`` on success, or *None*.
    """
    for bx, by in ASTC_BLOCKS:
        cs = astc_chain_size(dim_x, dim_y, bx, by, n_mips)
        if cs > 0 and payload_size % cs == 0:
            layers = payload_size // cs
            if 1 <= layers <= 256:
                return (bx, by), layers
    return None


def compute_content_hash(payload: bytes) -> bytes:
    """Compute the 16-byte MD5 digest used in the v8 content_hash field."""
    return hashlib.md5(payload).digest()
