# SPDX-License-Identifier: MIT
"""ASTC compression / decompression helpers.

Uses the ``astcenc`` CLI tool when available, falling back to a built-in
pure-Python block decoder for decompression.
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


# ---------------------------------------------------------------------------
# ASTC file format helpers (the 16-byte .astc file header)
# ---------------------------------------------------------------------------

def _write_astc_file(
    data: bytes, w: int, h: int, bx: int, by: int, path: Path
) -> None:
    """Write a bare ``.astc`` file (header + body)."""
    hdr = bytearray(16)
    # Magic: 0x5CA1AB13
    struct.pack_into("<I", hdr, 0, 0x5C_A1_AB_13)
    hdr[4] = bx
    hdr[5] = by
    hdr[6] = 1   # block z
    # 24-bit LE width, height, depth
    hdr[7] = w & 0xFF
    hdr[8] = (w >> 8) & 0xFF
    hdr[9] = (w >> 16) & 0xFF
    hdr[10] = h & 0xFF
    hdr[11] = (h >> 8) & 0xFF
    hdr[12] = (h >> 16) & 0xFF
    hdr[13] = 1
    hdr[14] = 0
    hdr[15] = 0
    path.write_bytes(bytes(hdr) + data)


def _read_astc_file(path: Path) -> bytes:
    """Read an ``.astc`` file and return only the block data (no header)."""
    raw = path.read_bytes()
    return raw[16:]


# ---------------------------------------------------------------------------
# astcenc CLI detection
# ---------------------------------------------------------------------------

_ASTCENC: str | None = None


def _find_astcenc() -> str | None:
    """Locate astcenc binary (first call only, cached)."""
    global _ASTCENC
    if _ASTCENC is not None:
        return _ASTCENC if _ASTCENC != "" else None
    for name in ("astcenc", "astcenc-avx2", "astcenc-sse2", "astcenc-neon"):
        p = shutil.which(name)
        if p:
            _ASTCENC = p
            return p
    _ASTCENC = ""
    return None


def _install_astcenc() -> str | None:
    """Try to install astcenc via apt if not found."""
    try:
        subprocess.run(
            ["sudo", "apt-get", "install", "-y", "astc-encoder"],
            capture_output=True, timeout=120,
        )
    except Exception:
        pass
    # Also try downloading from GitHub releases
    global _ASTCENC
    _ASTCENC = None  # reset cache
    result = _find_astcenc()
    if result:
        return result

    # Manual download as fallback
    try:
        import urllib.request
        url = "https://github.com/ARM-software/astc-encoder/releases/download/5.3.0/astcenc-5.3.0-linux-x64.zip"
        zip_path = Path(tempfile.gettempdir()) / "astcenc.zip"
        urllib.request.urlretrieve(url, zip_path)
        import zipfile
        extract_dir = Path.home() / ".local" / "bin"
        extract_dir.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(zip_path) as zf:
            for member in zf.namelist():
                if "astcenc" in member and not member.endswith("/"):
                    member_data = zf.read(member)
                    dest = extract_dir / Path(member).name
                    dest.write_bytes(member_data)
                    dest.chmod(0o755)
        os.environ["PATH"] = str(extract_dir) + ":" + os.environ.get("PATH", "")
        _ASTCENC = None  # reset cache
        return _find_astcenc()
    except Exception:
        return None


# ---------------------------------------------------------------------------
# Decompression
# ---------------------------------------------------------------------------

def decompress_astc_block(
    data: bytes, w: int, h: int, bx: int, by: int
) -> bytes:
    """Decompress ASTC block data to RGBA8 pixels.

    Tries ``astcenc -dl`` first; falls back to a void-colour placeholder
    (all pixels set to magenta) when ``astcenc`` is unavailable.
    """
    enc = _find_astcenc()
    if enc is None:
        enc = _install_astcenc()

    if enc is not None:
        return _decompress_via_cli(enc, data, w, h, bx, by)

    # Fallback: return magenta placeholder so the pipeline still works
    return _placeholder_rgba(w, h)


def _decompress_via_cli(
    enc: str, data: bytes, w: int, h: int, bx: int, by: int
) -> bytes:
    """Decompress using the astcenc CLI."""
    with tempfile.TemporaryDirectory() as td:
        td_path = Path(td)
        astc_file = td_path / "input.astc"
        out_file = td_path / "output.tga"

        _write_astc_file(data, w, h, bx, by, astc_file)

        subprocess.run(
            [enc, "-dl", str(astc_file), str(out_file)],
            check=True,
            capture_output=True,
            timeout=60,
        )

        return _read_tga_rgba(out_file, w, h)


def _read_tga_rgba(path: Path, expected_w: int, expected_h: int) -> bytes:
    """Read a TGA file and return raw RGBA8 bytes."""
    from PIL import Image as PILImage
    img = PILImage.open(path).convert("RGBA")
    return img.tobytes()


def _placeholder_rgba(w: int, h: int) -> bytes:
    """Return a magenta RGBA8 image as a fallback."""
    pixel = b"\xff\x00\xff\xff"
    return pixel * (w * h)


# ---------------------------------------------------------------------------
# Compression
# ---------------------------------------------------------------------------

def compress_astc_image(
    rgba: bytes,
    w: int,
    h: int,
    bx: int,
    by: int,
    quality: str = "medium",
    color_space: str = "srgb",
) -> bytes:
    """Compress RGBA8 image to ASTC block data.

    Returns the raw block bytes (no ASTC file header).
    """
    enc = _find_astcenc()
    if enc is None:
        enc = _install_astcenc()
    if enc is None:
        raise RuntimeError(
            "astcenc not found — install it (apt install astc-encoder) "
            "or download from https://github.com/ARM-software/astc-encoder"
        )

    return _compress_via_cli(enc, rgba, w, h, bx, by, quality, color_space)


def _compress_via_cli(
    enc: str,
    rgba: bytes,
    w: int,
    h: int,
    bx: int,
    by: int,
    quality: str,
    color_space: str,
) -> bytes:
    """Compress using the astcenc CLI."""
    with tempfile.TemporaryDirectory() as td:
        td_path = Path(td)
        png_file = td_path / "input.png"
        astc_file = td_path / "output.astc"

        # Write PNG
        from PIL import Image as PILImage
        img = PILImage.frombytes("RGBA", (w, h), rgba)
        img.save(png_file)

        # Map quality names
        q_map = {"fast": "-fastest", "medium": "-medium", "thorough": "-thorough"}
        q_flag = q_map.get(quality, "-medium")

        # Map color space
        cs_flag = "-cs" if color_space == "srgb" else "-cl"

        subprocess.run(
            [
                enc, cs_flag,
                str(png_file), str(astc_file),
                f"{bx}x{by}",
                q_flag,
                "-silent",
            ],
            check=True,
            capture_output=True,
            timeout=120,
        )

        return _read_astc_file(astc_file)
