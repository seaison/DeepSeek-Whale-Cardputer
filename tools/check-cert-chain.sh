#!/usr/bin/env bash
# 复核 api.deepseek.com 的证书链，是否由 firmware/DeepSeekWhale/src/root_ca.h 里内置的**某一个**根签发。
#
#   bash tools/check-cert-chain.sh              # 默认查 api.deepseek.com
#   bash tools/check-cert-chain.sh other.host   # 查别的域名
#
# 为什么要「某一个」：DeepSeek 在国内和海外走**不同**的证书链
#   · 中国大陆: api.deepseek.com <- TrustAsia DV TLS RSA CA 2025 <- DigiCert Global Root G2
#   · 海外    : *.deepseek.com   <- Amazon RSA 2048 M01          <- Amazon Root CA 1
# 所以固件里两个根都带着；这个脚本用来确认「链根确实在内置集合里」。
#
# 退出码：0 = 命中内置根；1 = 没命中（需要补根）；2 = 环境/网络问题
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${1:-api.deepseek.com}"
CA_FILE="$ROOT/firmware/DeepSeekWhale/src/root_ca.h"

for bin in openssl python3; do
  command -v "$bin" >/dev/null 2>&1 || { echo "需要 $bin" >&2; exit 2; }
done

echo "== $HOST 的证书链 =="
chain="$(echo | openssl s_client -connect "$HOST:443" -servername "$HOST" -showcerts 2>/dev/null \
         | grep -E '^ *[0-9]+ s:' || true)"
if [ -z "$chain" ]; then
  echo "拿不到证书链（网络不通或域名不可达）" >&2
  exit 2
fi
echo "$chain"

# 链上最后一个 subject 一般就是根（或被中间证书指到的根）
last_subject="$(echo "$chain" | tail -1 | sed 's/^ *[0-9]* s://')"
echo
echo "链根 subject: $last_subject"

echo
echo "== 内置根（firmware/DeepSeekWhale/src/root_ca.h）=="
inner_subjects="$(python3 - "$CA_FILE" <<'PY'
import re, subprocess, sys
text = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r"kRootCaPem\[\]\s*=\s*(.*?);", text, re.S)
if not m:
    sys.exit("解析不出 kRootCaPem")
pem = "".join(re.findall(r'"([^"]*)"', m.group(1))).replace("\\n", "\n")
blocks = [b for b in re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", pem, re.S)]
if not blocks:
    sys.exit("内置 PEM 里没有证书")
for b in blocks:
    out = subprocess.run(["openssl", "x509", "-noout", "-subject"],
                         input=b, capture_output=True, text=True)
    line = out.stdout.strip().removeprefix("subject=")
    print(line)
PY
)"
echo "$inner_subjects"

# 只比对 CN
cn_of() { echo "$1" | grep -o 'CN *= *[^,]*' | head -1 | sed 's/CN *= *//; s/ *$//'; }
chain_cn="$(cn_of "$last_subject")"
if [ -z "$chain_cn" ]; then
  echo "取不到链根的 CN，无法比对" >&2
  exit 2
fi

while IFS= read -r subj; do
  [ -z "$subj" ] && continue
  if [ "$(cn_of "$subj")" = "$chain_cn" ]; then
    echo
    echo "OK：链根 CN '$chain_cn' 命中内置根。"
    exit 0
  fi
done <<< "$inner_subjects"

echo
echo "没命中：链根 CN='$chain_cn' 不在内置根里。" >&2
echo "请下载对应的根证书 PEM 放进 tools/certs/，再跑：python3 tools/make-root-ca.py" >&2
exit 1
