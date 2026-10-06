#!/usr/bin/env python3
"""把裸位图 bin 还原成 PNG，用来肉眼校验尺寸/字节序是否猜对。

只依赖标准库（zlib + struct），不需要 Pillow。

  python3 tools/bin2png.py --input assets/whale_96x96_rgb565.bin \\
      --output /tmp/whale_96.png --width 96 --height 96 --format rgb565
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib

FORMATS = ("rgb565", "rgb565be", "gray8")


def chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def pixels(data: bytes, fmt: str, w: int, h: int):
    out = []
    if fmt in ("rgb565", "rgb565be"):
        order = "little" if fmt == "rgb565" else "big"
        for i in range(0, len(data), 2):
            v = int.from_bytes(data[i:i + 2], order)
            r = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            out.append(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    else:  # gray8
        for v in data:
            out.append((v, v, v))
    if len(out) != w * h:
        raise SystemExit(f"像素数 {len(out)} 与 {w}x{h}={w * h} 不符")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="裸位图 bin -> PNG 预览")
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--width", type=int, required=True)
    ap.add_argument("--height", type=int, required=True)
    ap.add_argument("--format", choices=FORMATS, default="rgb565")
    ap.add_argument("--scale", type=int, default=1, help="整数放大倍数，便于观察")
    args = ap.parse_args()

    with open(args.input, "rb") as fh:
        data = fh.read()

    px = pixels(data, args.format, args.width, args.height)
    s = max(1, args.scale)
    raw = bytearray()
    for y in range(args.height):
        for _ in range(s):
            raw.append(0)  # filter type 0
            row = px[y * args.width:(y + 1) * args.width]
            for p in row:
                raw += bytes(p) * s

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", args.width * s, args.height * s, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    with open(args.output, "wb") as fh:
        fh.write(png)
    print(f"已写出 {args.output} ({args.width * s}x{args.height * s})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
