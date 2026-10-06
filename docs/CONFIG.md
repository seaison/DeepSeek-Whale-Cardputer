# 配置与落盘文件

设备上所有可变状态都在 SD 卡的 `/dswhale/` 目录里（卡必须是 **FAT32 + MBR** 分区，[原因见这里](GETTING-STARTED.md#二sd-卡怎么格式化)）；**没插卡时配置会退回 NVS**（ESP32 flash 里的 Preferences），账本只在内存里（重启即丢，仅在卡不在时如此）。

```text
/dswhale/
├── config.json          # 配置（本文件）
├── config.json.tmp      # 写入时的临时文件，正常不会留下
├── ledger.json          # 账本（逐日已观测消费）
├── ledger.json.tmp
└── sound/
    ├── task_end.wav     # 可选：任务结束音（16bit PCM wav）
    └── key.wav          # 可选：预留
```

## config.json

开机时会读取；**文件不存在时会自动写一份模板**（方便你在电脑上直接改）。改完可以在菜单里 `Reload config` 生效，或者重启设备。

| 字段 | 类型 | 默认 | 范围 | 说明 |
|---|---|---|---|---|
| `wifi.ssid` | string | `""` | — | WiFi 名称。**留空 = 不联网**，界面会提示没有配置 |
| `wifi.pass` | string | `""` | — | WiFi 密码 |
| `api_key` | string | `""` | — | DeepSeek API key（`sk-...`）。只用于 `GET /user/balance`，**只读接口、不产生费用** |
| `refresh_sec` | int | `60` | 15–900 | 余额刷新间隔（秒）。60 秒 ≈ 每天 1440 次请求 |
| `brightness` | int | `128` | 16–255 | 屏幕亮度 |
| `sound` | bool | `true` | — | 音效总开关 |
| `volume` | int | `110` | 0–255 | 音量 |
| `tls_verify` | bool | `true` | — | 是否校验 `api.deepseek.com` 的根证书。`firmware/DeepSeekWhale/src/root_ca.h` 内置**两个**根（DigiCert Global Root G2 与 Amazon Root CA 1）——DeepSeek 在国内与海外走不同证书链，两个都要带。**注意**：证书校验需要正确时间，NTP 未同步时会自动跳过校验 |
| `ledger_keep_days` | int | `90` | 1–3650 | 账本保留天数（与上游 90 天口径一致），超期逐日记录会被裁掉 |
| `bubble_auto_close_sec` | int | `12` | 0–120 | 气泡自动关闭秒数，`0` = 不自动关（按键才关） |
| `show_seconds` | bool | `true` | — | 状态栏时钟是否显示秒 |
| `lang` | string | `"en"` | `"en"` / `"zh"` | 界面语言。`"zh"` 走 M5GFX 自带的 efont 中文字库；设备上也能切：菜单 → `设置 → 语言`，或主屏按 `L` |
| `whale_spin` | bool | `true` | — | 点按鲸鱼（或按 `ENTER`/空格）时是否播放 360° 旋转动画 |

### 示例

```json
{
  "wifi": { "ssid": "HomeWiFi", "pass": "hunter2hunter2" },
  "api_key": "sk-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  "refresh_sec": 120,
  "brightness": 160,
  "sound": true,
  "volume": 90,
  "tls_verify": true,
  "ledger_keep_days": 90,
  "bubble_auto_close_sec": 12,
  "show_seconds": true,
  "lang": "zh",
  "whale_spin": true
}
```

> ⚠️ **API key 是明文存在 SD 卡上的**。卡丢了等于 key 泄露——介意的话就去 platform 生成一把**只用于查询余额**的低权限 key，或者用完拔卡。设备端没有别的安全存储可用（ESP32 的 NVS 加密需要额外配置 eFuse，本工程没做）。

### 没有 SD 卡时

NVS 里只存四个字段（`ssid` / `pass` / `key` / `lang`），因为容量和接口都有限：

- 首次烧录后如果没插卡，用**菜单 → Settings** 只能改亮度/音量这些运行时项，改完写入 NVS 的也只有上面三项；
- 想完整配置（刷新间隔、TLS、账本天数……）还是需要 SD 卡。

## ledger.json

记账内核自动维护，一般不用手改。结构：

```json
{
  "v": 1,
  "active": "3f2a9c1b8d4e5f60-CNY",
  "books": {
    "3f2a9c1b8d4e5f60-CNY": {
      "cur": "CNY",
      "lastAt": 1791234567,
      "days": {
        "2026-10-06": { "f": 1791200000, "l": 1791234567, "o": 11000000000, "u": 10990000000, "d": 10000000, "c": 0 }
      }
    }
  }
}
```

| 键 | 含义 |
|---|---|
| `active` | 当前记账本 = `<密钥指纹>-<币种>` |
| `books` | 按**密钥指纹**分本：换 API key 等于换一本账，旧本保留、**不跨本相加**（与上游一致） |
| `cur` | 币种 |
| `lastAt` | 这本账最后一次观测时间（UTC 秒），用于丢弃乱序/重复样本 |
| `days` | 逐日记录，key 为**北京时间**日期 |
| `f` / `l` | 当天第一次 / 最后一次观测时间（UTC 秒） |
| `o` / `u` | 当天起点余额 / 最近一次余额（定点，1e-8 元） |
| `d` / `c` | 当天累计**下降**（消费）/ **上升**（充值、赠金），定点 |

金额单位是 **1e-8 元**（`10000000` = 0.1 元），与上游 `accounting.mjs` 的 `SCALE` 完全一致。

**写入是原子的**：先写 `ledger.json.tmp`，再 `remove` + `rename`，避免掉电/拔卡把账本写坏。写入节流为「最多 30 秒一次」，另外进入设置页改配置时会顺带落盘。

想清空账本：删掉 `ledger.json`（或把 `books` 里那本删掉）后重启。下次观测会以当时的余额作为新起点。

## 自定义音效

设备端默认用**合成音**（不打包任何第三方素材）。想换成自己的声音，把 16bit PCM 的 wav 放到：

```text
/dswhale/sound/
├── press.wav      # 按键按下
├── release.wav    # 按键松开 / 返回
├── click.wav      # 点按鲸鱼（插件里对应「按压音效」，比如 Ya1.mp3）
└── task_end.wav   # 任务结束音（比如 minecraft-exp-orb.wav）
```

**文件存在就用它，不存在自动回落到内置合成音**（所以缺哪个都不影响使用）。
播放时整段读进 PSRAM（单个文件上限 4MB）。

上游插件的音效是 mp3，**设备端没有 mp3 解码器**，必须先转成 16bit PCM 的 wav：

```bash
# 以插件的「小黄鸭」按压音为例
ffmpeg -i Ya1.mp3 -ac 1 -ar 16000 -sample_fmt s16 /Volumes/SD/dswhale/sound/click.wav
ffmpeg -i Ya2.mp3 -ac 1 -ar 16000 -sample_fmt s16 /Volumes/SD/dswhale/sound/release.wav
ffmpeg -i minecraft-exp-orb.wav -ac 1 -ar 16000 -sample_fmt s16 /Volumes/SD/dswhale/sound/task_end.wav
```

音量用 `volume` 字段控制（同时作用于合成音和 wav）。
