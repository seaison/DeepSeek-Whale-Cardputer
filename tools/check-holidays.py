#!/usr/bin/env python3
"""峰谷节假日表自检（CI 里跑）。

DeepSeek 的峰谷规则是「工作日高峰、周末与**中国法定节假日**全天谷价」，
所以每年 11 月国务院发布次年安排后，必须把新日期补进 firmware/DeepSeekWhale/src/pricing.cpp 的
kHolidayValley 表。这个脚本负责在过期前提醒。

  python3 tools/check-holidays.py            # 检查
  python3 tools/check-holidays.py --strict   # 需要补表时退出码 1（CI 用）
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRICING = ROOT / "firmware" / "DeepSeekWhale" / "src" / "pricing.cpp"


def load_dates(text: str) -> list[str]:
    block = re.search(r"kHolidayValley\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not block:
        raise SystemExit("在 firmware/DeepSeekWhale/src/pricing.cpp 里找不到 kHolidayValley 表")
    return re.findall(r'"(\d{4}-\d{2}-\d{2})"', block.group(1))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--strict", action="store_true", help="需要补表时返回非零退出码")
    ap.add_argument("--today", help="覆盖“今天”（YYYY-MM-DD），便于测试")
    args = ap.parse_args()

    dates = load_dates(PRICING.read_text(encoding="utf-8"))
    if not dates:
        print("::error::节假日表是空的", file=sys.stderr)
        return 1

    years = sorted({d[:4] for d in dates})
    today = dt.date.fromisoformat(args.today) if args.today else dt.date.today()

    print(f"节假日表覆盖 {len(dates)} 天，年份: {', '.join(years)}")
    for y in years:
        per_year = sorted(d for d in dates if d.startswith(y))
        print(f"  {y}: {len(per_year)} 天（{per_year[0]} … {per_year[-1]}）")

    need: list[str] = []
    if str(today.year) not in years:
        need.append(f"当年 {today.year} 没有任何节假日条目")
    # 国务院一般在 11 月发布次年安排
    if today.month >= 11 and str(today.year + 1) not in years:
        need.append(f"已到 {today.month} 月，但次年 {today.year + 1} 的放假安排还没补进表里")

    # 重复项
    dupes = {d for d in dates if dates.count(d) > 1}
    if dupes:
        need.append("重复条目: " + ", ".join(sorted(dupes)))

    if need:
        for n in need:
            print(f"::warning::{n}", file=sys.stderr)
        print("\n请更新 firmware/DeepSeekWhale/src/pricing.cpp 的 kHolidayValley 表（依据国务院办公厅通知）。")
        return 1 if args.strict else 0

    print("OK：节假日表覆盖当前与下一年度。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
