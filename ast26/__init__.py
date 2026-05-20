# SPDX-License-Identifier: MIT
"""ast26 — read, write and inspect Farming Simulator 2026 AST (GS2D v8) textures."""

from ast26.container import (
    Header,
    parse_header,
    write_header,
    HEADER_SIZE,
)
from ast26.codec import (
    decode,
    encode,
    inspect_file,
    DecodedImage,
    InspectResult,
)

__version__ = "1.0.0"

__all__ = [
    "Header",
    "parse_header",
    "write_header",
    "HEADER_SIZE",
    "decode",
    "encode",
    "inspect_file",
    "DecodedImage",
    "InspectResult",
    "__version__",
]
