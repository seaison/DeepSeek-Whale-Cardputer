#!/usr/bin/env python3
"""把 tools/certs/*.pem 合成 src/root_ca.h。

为什么要多个根：**api.deepseek.com 的证书链随地区变化**（实测）：

  中国大陆   api.deepseek.com  <- TrustAsia DV TLS RSA CA 2025 <- DigiCert Global Root G2
  海外（US）  *.deepseek.com    <- Amazon RSA 2048 M01         <- Amazon Root CA 1

只钉一个根，另一边的用户就会握手失败。arduino-esp32 的
`WiFiClientSecure::setCACert()` 最终调 `mbedtls_x509_crt_parse()`，
而 mbedtls 支持「一段 PEM 里包含多张证书」，所以这里直接把所有根拼成一串。

  python3 tools/make-root-ca.py            # 写 src/root_ca.h
  python3 tools/make-root-ca.py --check    # 只校验仓库里的那份是否一致（CI 用）
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CERTS = ROOT / "tools" / "certs"
OUT = ROOT / "src" / "root_ca.h"

# 证书文件 -> 注释里显示的地区标签
LABELS = {
    "digicert-global-root-g2.pem": "DigiCert Global Root G2 —— 中国大陆实测链路",
    "amazon-root-ca1.pem": "Amazon Root CA 1 —— 海外实测链路（CloudFront / Amazon RSA 2048 M01）",
}

HEADER = """// 由 tools/make-root-ca.py 自动生成，请勿手工编辑。
//
// api.deepseek.com 的证书链**随地区不同**（实测）：
//   中国大陆   api.deepseek.com <- TrustAsia DV TLS RSA CA 2025 <- DigiCert Global Root G2
//   海外       *.deepseek.com   <- Amazon RSA 2048 M01          <- Amazon Root CA 1
// 只内置一个根，另一边的用户就会握手失败，所以两个都带上：拼接成一段 PEM，
// 由 mbedtls_x509_crt_parse() 一次解析多张证书（arduino-esp32 的
// NetworkClientSecure::setCACert() 就是这么用的）。
//
// 想不校验（被中间人代理 / 校园网）就在 config.json 里设 "tls_verify": false。
// 复核：bash tools/check-cert-chain.sh
// 换根：把新 PEM 放进 tools/certs/，再跑 python3 tools/make-root-ca.py
#pragma once

static const char kRootCaPem[] =
{body};
"""


def pem_lines(pem: str, label: str) -> list[str]:
    out = [f'    // ---- {label} ----']
    for line in pem.strip().splitlines():
        out.append(f'    "{line}\\n"')
    return out


def build() -> str:
    pems = sorted(CERTS.glob("*.pem"))
    if not pems:
        raise SystemExit(f"{CERTS} 下没有 .pem 文件")
    body: list[str] = []
    for i, path in enumerate(pems):
        text = path.read_text(encoding="utf-8")
        if "BEGIN CERTIFICATE" not in text:
            raise SystemExit(f"{path} 看起来不是 PEM 证书")
        body += pem_lines(text, LABELS.get(path.name, path.name))
    return HEADER.format(body="\n".join(body))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只校验，不写入")
    args = ap.parse_args()

    want = build()
    if args.check:
        have = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if have != want:
            print(f"{OUT} 与 tools/certs/*.pem 不一致，请跑：python3 tools/make-root-ca.py", file=sys.stderr)
            return 1
        print(f"OK：{OUT} 与 tools/certs/*.pem 一致")
        return 0

    OUT.write_text(want, encoding="utf-8")
    print(f"已写出 {OUT}（{len(list(CERTS.glob('*.pem')))} 个根证书）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
