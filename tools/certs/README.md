# tools/certs/ — 内置根证书

这些 PEM 会被 [`tools/make-root-ca.py`](../make-root-ca.py) 合成 `src/root_ca.h`，
用于固件里 `WiFiClientSecure::setCACert()` 校验 `api.deepseek.com` 的证书链。

**为什么要两个：** DeepSeek 在国内和海外走的是**不同**的证书链（2026-10 实测）：

| 出口 | 服务器证书 | 中间证书 | 链根 |
|---|---|---|---|
| 中国大陆 | `api.deepseek.com` | TrustAsia DV TLS RSA CA 2025 | **DigiCert Global Root G2** |
| 海外（如 GitHub Actions 美国 runner） | `*.deepseek.com` | Amazon RSA 2048 M01 | **Amazon Root CA 1** |

只钉一个根，另一边就会握手失败——这个坑是 CI 第一次跑就踩出来的
（见 commit 里的 `fix(tls)`），所以两个都带上。

## 文件

| 文件 | 来源 | 有效期 |
|---|---|---|
| `digicert-global-root-g2.pem` | <https://cacerts.digicert.com/DigiCertGlobalRootG2.crt.pem> | 2013-08-01 → 2038-01-15 |
| `amazon-root-ca1.pem` | <https://www.amazontrust.com/repository/AmazonRootCA1.pem> | 2015-05-26 → 2038-01-17 |

两者都是公开的根证书（自签），放在这里只是为了可复现地生成头文件。

## 复核

```bash
# 1) 联网确认当前链根命中内置集合（CI 会跑；本地在国内/海外跑的结果可能不同，都算通过）
bash tools/check-cert-chain.sh

# 2) 确认 src/root_ca.h 与这些 PEM 一致（CI 会跑）
python3 tools/make-root-ca.py --check
```

指纹（SHA-256，可用于交叉核对）：

```bash
openssl x509 -in tools/certs/digicert-global-root-g2.pem -noout -fingerprint -sha256
openssl x509 -in tools/certs/amazon-root-ca1.pem  -noout -fingerprint -sha256
```

## 换根

1. 把新根 PEM 放进本目录；
2. `python3 tools/make-root-ca.py`；
3. 提交新 PEM + 重新生成的 `src/root_ca.h`；
4. 如果旧的根确实不再被用到，可以在同一次提交里删掉——`make-root-ca.py` 是按目录里
   现有文件全量生成的，文件在就在，删了就不在。
