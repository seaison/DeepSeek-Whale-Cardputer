#!/usr/bin/env bash
# 主机侧单元测试：不需要 ESP32、不需要 arduino-cli。
#
#   bash tests/run.sh
#
# 可选环境变量：CXX（默认 g++，macOS 上会退到 clang++）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [ -z "${CXX:-}" ]; then
  if command -v g++ >/dev/null 2>&1; then
    CXX=g++
  elif command -v clang++ >/dev/null 2>&1; then
    CXX=clang++
  else
    echo "需要 g++ 或 clang++" >&2
    exit 2
  fi
fi

OUT_DIR="${TMPDIR:-/tmp}/dswhale-tests"
mkdir -p "$OUT_DIR"
BIN="$OUT_DIR/run_tests"

echo "== 编译 (${CXX}) =="
"$CXX" -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter \
  -I"$ROOT/firmware/DeepSeekWhale/src" \
  tests/native/run_tests.cpp firmware/DeepSeekWhale/src/pricing.cpp \
  -o "$BIN"

echo "== 运行 =="
"$BIN" tests/golden/peak-2026.tsv
