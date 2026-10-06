#!/usr/bin/env bash
# 复核 api.deepseek.com 的证书链是否仍由 src/root_ca.h 里内置的根签发。
#
#   bash tools/check-cert-chain.sh
#
# 退出码：0 = 链根与内置根一致；1 = 不一致（需要更新 root_ca.h）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${1:-api.deepseek.com}"
CA_FILE="$ROOT/src/root_ca.h"

if ! command -v openssl >/dev/null 2>&1; then
  echo "需要 openssl" >&2
  exit 2
fi

echo "== $HOST 的证书链 =="
chain="$(echo | openssl s_client -connect "$HOST:443" -servername "$HOST" -showcerts 2>/dev/null \
         | grep -E '^ *[0-9]+ s:' || true)"
if [ -z "$chain" ]; then
  echo "拿不到证书链（网络不通或该域名不可达）" >&2
  exit 2
fi
echo "$chain"

# 链上最后一个 subject 一般就是根（或被中间证书指到的根）
last_subject="$(echo "$chain" | tail -1 | sed 's/^ *[0-9]* s://')"
echo
echo "链根 subject: $last_subject"

echo
echo "== 内置根（src/root_ca.h）=="
if ! command -v python3 >/dev/null 2>&1; then
  echo "需要 python3 来解析 root_ca.h（或手工比对 grep -A2 BEGIN src/root_ca.h）" >&2
  exit 2
fi
inner_pem="$(python3 - "$CA_FILE" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r"kRootCaPem\[\]\s*=\s*(.*?);", text, re.S)
if not m:
    sys.exit("解析不出 kRootCaPem")
pem = "".join(re.findall(r'"([^"]*)"', m.group(1)))
sys.stdout.write(pem.replace("\\n", "\n"))
PY
)"
if [ -z "$inner_pem" ]; then
  echo "解析不出内置证书" >&2
  exit 2
fi
inner_subject="$(printf '%s\n' "$inner_pem" | openssl x509 -noout -subject 2>/dev/null || true)"
echo "内置 subject: $inner_subject"

# 只比对 CN
cn_of() { echo "$1" | grep -o 'CN *= *[^,]*' | head -1 | sed 's/CN *= *//'; }
a="$(cn_of "$last_subject")"
b="$(cn_of "$inner_subject")"

if [ "$a" = "$b" ] && [ -n "$a" ]; then
  echo
  echo "OK：链根 CN '$a' 与内置根一致。"
  exit 0
fi

echo
echo "不一致：链根 CN='$a'，内置根 CN='$b'" >&2
echo "请到 https://cacerts.digicert.com/ 下载对应的根证书 PEM，替换 src/root_ca.h。" >&2
exit 1
