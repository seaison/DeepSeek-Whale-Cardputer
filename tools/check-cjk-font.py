#!/usr/bin/env python3
"""检查界面中文用到的字，是否都在 M5GFX 内置 efont 字库里。

M5GFX 的 `fonts::efontCN_*` 是 **U8g2 格式的子集字体**（cn 系列约 7545 个字形），
不是全字集。少一个字在屏幕上就是一块空白，而这种问题只有真机肉眼能看出来——
所以放一个静态检查在这里。

原理：直接从 `M5GFX/src/lgfx/Fonts/efont/lgfx_efont_cn.c` 里解析 U8g2 字体的
unicode 查找表（格式见 M5GFX 的 `U8g2font::getGlyph()`）：

    头部 23 字节；[21..22] 大端 = unicode 表相对第 23 字节的偏移
    unicode 表条目 4 字节：{大端 offset 增量, 大端 起始编码}
    第 i 块的字形数据 = 表基址 + 前 i+1 个 offset 增量之和
    每块里是 {大端编码, 1 字节长度, 数据…}，编码为 0 结束

用法：
  python3 tools/check-cjk-font.py                 # 自动找 M5GFX
  python3 tools/check-cjk-font.py --m5gfx <目录>  # 指定 M5GFX 根目录
  python3 tools/check-cjk-font.py --strict        # 找不到字库时返回非零（CI 用）
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LANG_SRC = ROOT / "firmware" / "DeepSeekWhale" / "src"
# 界面文案分散在两个文件：lang.h 的 DSW_STRINGS 宏表 + lang.cpp 的鲸鱼台词表
SCAN_FILES = [LANG_SRC / "lang.h", LANG_SRC / "lang.cpp"]

# 界面上实际用到的字号（改 lang.h 的 FontSet 时同步这里）
FONT_SYMBOLS = ["lgfx_efont_cn_12", "lgfx_efont_cn_16", "lgfx_efont_cn_24"]

M5GFX_CANDIDATES = [
    pathlib.Path.home() / "Documents" / "Arduino" / "libraries" / "M5GFX",
    pathlib.Path.home() / "Arduino" / "libraries" / "M5GFX",
    pathlib.Path.home() / ".arduino15" / "libraries" / "M5GFX",
]


def find_m5gfx(explicit: str | None) -> pathlib.Path | None:
    if explicit:
        p = pathlib.Path(explicit).expanduser()
        return p if (p / "src").is_dir() else None
    for p in M5GFX_CANDIDATES:
        if (p / "src" / "lgfx" / "Fonts" / "efont").is_dir():
            return p
    # esp32 core 里也可能带一份
    for base in (pathlib.Path.home() / "Library" / "Arduino15" / "packages").glob(
        "esp32/hardware/esp32/*/libraries/M5GFX"
    ):
        if (base / "src").is_dir():
            return base
    return None


def decode_c_literals(text: str, start: int, declared: int) -> bytes:
    """从 `= ` 之后开始，逐个吃掉 C 字符串字面量，直到遇到引号外的 `;`。

    不能简单找第一个 `;`：字库数据里会出现**字面量分号**（编译器只对少数字符做八进制转义，
    像字母、`[`、`;` 都是原样写进 .c 的）。
    """
    out = bytearray()
    i = start
    n = len(text)
    while i < n:
        c = text[i]
        if c == ";":
            break
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                ch = text[i]
                if ch != "\\":
                    out.append(ord(ch) & 0xFF)
                    i += 1
                    continue
                i += 1
                if i >= n:
                    break
                e = text[i]
                if e in "01234567":
                    digits = ""
                    while i < n and len(digits) < 3 and text[i] in "01234567":
                        digits += text[i]
                        i += 1
                    out.append(int(digits, 8) & 0xFF)
                elif e == "x":
                    i += 1
                    hexd = ""
                    while i < n and text[i] in "0123456789abcdefABCDEF":
                        hexd += text[i]
                        i += 1
                    out.append(int(hexd or "0", 16) & 0xFF)
                elif e == "\n":
                    i += 1  # 反斜杠续行：不产生字节
                elif e == "\r":
                    i += 1
                    if i < n and text[i] == "\n":
                        i += 1
                else:
                    out.append(
                        {"n": 10, "t": 9, "r": 13, "\\": 92, '"': 34, "'": 39}.get(e, ord(e))
                    )
                    i += 1
            i += 1  # 收尾引号
            continue
        i += 1

    # C 里用字符串字面量初始化 char 数组时，会在末尾多带一个隐式 '\0'，
    # 而数组声明的长度把它算进去了（和用 gcc 编出来的目标文件逐字节比对过）。
    if len(out) == declared - 1:
        out.append(0)
    if len(out) != declared:
        raise SystemExit(f"解出 {len(out)} 字节，声明是 {declared}")
    return bytes(out)


def load_font(c_file: pathlib.Path, symbol: str) -> bytes:
    text = c_file.read_text(encoding="latin-1")
    m = re.search(rf"PROGMEM const uint8_t {symbol}\[(\d+)\]\s*=", text)
    if not m:
        raise SystemExit(f"{c_file} 里找不到 {symbol}")
    return decode_c_literals(text, m.end(), int(m.group(1)))


def encodings_of(data: bytes) -> set[int]:
    """按 U8g2 格式枚举这张字体的全部编码。"""
    lut_base = 23 + ((data[21] << 8) | data[22])
    first_delta = (data[lut_base] << 8) | data[lut_base + 1]
    if first_delta == 0 or first_delta % 4:
        raise SystemExit(f"unicode 表大小异常: {first_delta}")
    entries = first_delta // 4

    out: set[int] = set()
    offset = 0
    prev_start = -1
    for i in range(entries):
        delta = (data[lut_base + i * 4] << 8) | data[lut_base + i * 4 + 1]
        start = (data[lut_base + i * 4 + 2] << 8) | data[lut_base + i * 4 + 3]
        if start < prev_start:
            raise SystemExit(f"unicode 表不是升序: {start} < {prev_start}")
        prev_start = start
        offset += delta
        p = lut_base + offset
        while True:
            enc = (data[p] << 8) | data[p + 1]
            if enc == 0:
                break
            out.add(enc)
            # ⚠️ 这个长度字节是**整条记录**的字节数（含 3 字节头），不是数据长度——
            #    对应 M5GFX `U8g2font::getGlyph()` 里的 `font += pgm_read_byte(&font[2])`。
            #    ASCII 区同理（那边是 2 字节头，`font += pgm_read_byte(&font[1])`）。
            p += data[p + 2]
    return out


# DSW_STRINGS 的一行：X(Name, "en", "zh")，可能因换行被反斜杠拆开
ROW_RE = re.compile(
    r'X\(\s*(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"',
    re.S,
)
# lang.cpp 里的鲸鱼台词：{weight, "en", "zh"}
PAIR_RE = re.compile(r'\{\s*\d+\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}', re.S)


def read_table() -> list[tuple[str, str, str, str]]:
    """返回 [(名字, 英文, 中文, 位置)]，来自 lang.h 的 DSW_STRINGS 表。"""
    text = (LANG_SRC / "lang.h").read_text(encoding="utf-8")
    start = text.index("#define DSW_STRINGS(X)")
    end = text.index("enum class Str", start)
    block = text[start:end].replace("\\\n", " ")  # 去掉续行反斜杠
    rows = []
    for m in ROW_RE.finditer(block):
        line = text[: start + m.start()].count("\n") + 1
        rows.append((m.group(1), m.group(2), m.group(3), f"lang.h:{line}"))
    return rows


def read_bubbles() -> list[tuple[str, str, str]]:
    """返回 [(英文, 中文, 位置)]，来自 lang.cpp 的 kBubblePairs 表。"""
    text = (LANG_SRC / "lang.cpp").read_text(encoding="utf-8")
    start = text.index("const BubblePair kBubblePairs[]")
    end = text.index("};", start)
    block = text[start:end]
    out = []
    for m in PAIR_RE.finditer(block):
        line = text[: start + m.start()].count("\n") + 1
        out.append((m.group(1), m.group(2), f"lang.cpp:{line}"))
    return out


def non_ascii(text_: str) -> list[str]:
    return [c for c in text_ if ord(c) > 0x7F]


def escape_preview(text_: str) -> str:
    return text_.encode("unicode_escape").decode("ascii")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--m5gfx", help="M5GFX 库目录（含 src/）")
    ap.add_argument("--strict", action="store_true", help="找不到字库时返回非零")
    args = ap.parse_args()

    m5gfx = find_m5gfx(args.m5gfx)
    if m5gfx is None:
        msg = "找不到 M5GFX 库，跳过中文字形检查（用 --m5gfx 指定，或先装库）"
        print(f"::warning::{msg}" if args.strict else msg, file=sys.stderr)
        return 1 if args.strict else 0

    c_file = m5gfx / "src" / "lgfx" / "Fonts" / "efont" / "lgfx_efont_cn.c"
    if not c_file.is_file():
        print(f"::warning::{c_file} 不存在，跳过", file=sys.stderr)
        return 1 if args.strict else 0

    per_font: dict[str, set[int]] = {}
    for sym in FONT_SYMBOLS:
        per_font[sym] = encodings_of(load_font(c_file, sym))
        print(f"{sym}: {len(per_font[sym])} 个字形")

    rows = read_table()
    pairs = read_bubbles()
    print(f"\n文案表：DSW_STRINGS {len(rows)} 条，鲸鱼台词 {len(pairs)} 条")

    problems: list[str] = []

    # 规则 1：英文文案必须是纯 ASCII —— 英文界面用的是 ASCII 点阵/矢量字体，
    #         出现非 ASCII 就是方块或空白（历史上 · 就踩过这个坑）。
    for name, en, zh, where in rows:
        bad = non_ascii(en)
        if bad:
            problems.append(
                f"{where} [{name}] 英文文案含非 ASCII 字符 "
                f"{' '.join(f'{c}=U+{ord(c):04X}' for c in sorted(set(bad)))}"
                f"  -> {escape_preview(en)}"
            )
    for en, zh, where in pairs:
        bad = non_ascii(en)
        if bad:
            problems.append(
                f"{where} 英文台词含非 ASCII 字符 "
                f"{' '.join(f'{c}=U+{ord(c):04X}' for c in sorted(set(bad)))}"
                f"  -> {escape_preview(en)}"
            )

    # 规则 2：中文文案的每个非 ASCII 字符都要在（每一档）中文字库里
    used: dict[str, list[str]] = {}
    for name, en, zh, where in rows:
        for ch in non_ascii(zh):
            used.setdefault(ch, []).append(f"{where}[{name}]")
    for en, zh, where in pairs:
        for ch in non_ascii(zh):
            used.setdefault(ch, []).append(where)

    for ch, wheres in sorted(used.items(), key=lambda kv: ord(kv[0])):
        missing = [s for s, e in per_font.items() if ord(ch) not in e]
        if missing:
            short = ", ".join(m.replace("lgfx_efont_cn_", "") for m in missing)
            problems.append(
                f"字符 {ch} (U+{ord(ch):04X}) 在字号 [{short}] 里缺失；"
                f"被 {len(wheres)} 处引用，例如 {wheres[0]}"
            )

    print(f"中文文案用到 {len(used)} 个不同的非 ASCII 字符")

    if problems:
        print("\n发现问题:", file=sys.stderr)
        for p_ in problems:
            print(f"  - {p_}", file=sys.stderr)
        return 1

    print("OK：英文文案纯 ASCII，中文文案的字形全部命中 efont 字库。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
