#!/usr/bin/env python3
"""把裸位图 bin 转成 Arduino 可用的 C 头文件。

支持格式（--format）：
  rgb565    小端 RGB565，每像素 2 字节（本项目默认，M5GFX pushImage 用）
  rgb565be  大端 RGB565
  gray8     8 位灰度（M5GFX pushGray / 需要自行转 RGB565）
  mono1     1 位单色，MSB 在前，行按字节对齐（image2cpp 风格）

用法：
  python3 tools/bin2header.py --input assets/whale_96x96_rgb565.bin \\
      --output firmware/DeepSeekWhale/src/assets/whale_96.h --width 96 --height 96 --name whale_96

校验规则（会拒绝明显对不上的输入，避免生成坏数组）：
  rgb565*: len == w * h * 2
  gray8  : len == w * h
  mono1  : len == ceil(w / 8) * h
"""

from __future__ import annotations

import argparse
import os
import sys

FORMATS = ("rgb565", "rgb565be", "gray8", "mono1")

HEADER_TMPL = """// 由 tools/bin2header.py 自动生成，请勿手工编辑。
// 源文件: {src}
// 格式: {fmt}  尺寸: {w}x{h}  字节: {size}
#pragma once

#include <stdint.h>

#define {NAME}_W {w}
#define {NAME}_H {h}
#define {NAME}_PIXELS ({w} * {h})

// ESP32 上 const 全局量直接落在 flash (.rodata)，无需 PROGMEM。
static const {ctype} {name}[{count}] = {{
{body}
}};
"""


def expected_len(fmt: str, w: int, h: int) -> int:
    if fmt in ("rgb565", "rgb565be"):
        return w * h * 2
    if fmt == "gray8":
        return w * h
    if fmt == "mono1":
        return ((w + 7) // 8) * h
    raise ValueError(f"未知格式: {fmt}")


def load_words(data: bytes, fmt: str) -> tuple[str, list[int]]:
    """返回 (C 类型, 数值列表)。"""
    if fmt in ("rgb565", "rgb565be"):
        order = "little" if fmt == "rgb565" else "big"
        vals = [int.from_bytes(data[i:i + 2], order) for i in range(0, len(data), 2)]
        return "uint16_t", vals
    return "uint8_t", list(data)


def main() -> int:
    ap = argparse.ArgumentParser(description="裸位图 bin -> C 头文件")
    ap.add_argument("--input", required=True, help="输入 bin")
    ap.add_argument("--output", required=True, help="输出 .h")
    ap.add_argument("--width", type=int, required=True)
    ap.add_argument("--height", type=int, required=True)
    ap.add_argument("--name", required=True, help="C 数组名，如 whale_96")
    ap.add_argument("--format", choices=FORMATS, default="rgb565")
    ap.add_argument("--per-line", type=int, default=16, help="每行几个数值")
    args = ap.parse_args()

    with open(args.input, "rb") as fh:
        data = fh.read()

    want = expected_len(args.format, args.width, args.height)
    if len(data) != want:
        print(
            f"错误: {args.input} 有 {len(data)} 字节，但 {args.width}x{args.height} 的 "
            f"{args.format} 应为 {want} 字节",
            file=sys.stderr,
        )
        return 1

    ctype, vals = load_words(data, args.format)
    width = 2 if ctype == "uint8_t" else 4
    lines = []
    for i in range(0, len(vals), args.per_line):
        chunk = vals[i:i + args.per_line]
        lines.append("    " + "".join(f"0x{v:0{width}x}," for v in chunk))

    text = HEADER_TMPL.format(
        src=os.path.basename(args.input),
        fmt=args.format,
        w=args.width,
        h=args.height,
        size=len(data),
        name=args.name,
        NAME=args.name.upper(),
        ctype=ctype,
        count=len(vals),
        body="\n".join(lines),
    )
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"已生成 {args.output}: {ctype} {args.name}[{len(vals)}] ({len(data)} 字节源数据)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
