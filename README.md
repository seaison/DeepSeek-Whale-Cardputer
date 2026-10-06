<div align="center">

# DeepSeek Whale · M5Stack Cardputer ADV

**把 DSH 网页右下角的「余额小鲸鱼挂件」搬到掌上设备上。**

<img src="docs/images/whale_96.png" width="180" alt="whale">

[![CI](https://github.com/seaison/DeepSeek-Whale-Cardputer/actions/workflows/ci.yml/badge.svg)](https://github.com/seaison/DeepSeek-Whale-Cardputer/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Board](https://img.shields.io/badge/board-M5Cardputer%20ADV-4D6BFE)
![Core](https://img.shields.io/badge/arduino--esp32-3.3.x-informational)

</div>

---

## 这是什么

[DeepSeek-Balance-Whale-Widget](https://github.com/MeteorNOX/DeepSeek-Balance-Whale-Widget) 是挂在 DSH（DeepSeek Harness）Web 界面右下角的小鲸鱼：显示 DeepSeek API 余额、今日已用、峰谷时段，点一下还会冒泡吐槽。

本工程把它**移植成 M5Stack Cardputer ADV 上的独立固件**——不用开电脑、不用跑 DSH，开机就能看余额。记账内核、峰谷规则、定价表都按上游口径逐条对齐（定点金额、按北京时间分日、周末与法定节假日谷价）。

## 功能

| 功能 | 说明 |
|---|---|
| 💰 **余额** | `GET https://api.deepseek.com/user/balance`，显示总余额 / 赠金 / 充值余额；默认 60 秒刷新，可手动刷新 |
| 📊 **今日已用** | 余额观测记账：余额**下降**累计为消费、**上升**单独记为充值，互不冲抵；金额按 `1e-8` 元定点累计，按北京时间分日 |
| ⛰️ **峰谷时段** | 工作日 9:00–12:00 / 14:00–18:00 为高峰（双倍价），周末与中国法定节假日全天谷价；主屏显示当前档位 + **下一切换点倒计时** |
| 🐋 **鲸鱼冒泡** | 点按鲸鱼 / `ENTER`：余额 → 今日已用 → 峰谷 → 随机台词，队列式推进（30 秒不点回到第 1 项），可设自动关闭秒数 |
| ⌨️ **键盘菜单** | 刷新 / 冒泡 / 账本 / 网络 / 设置 / 关于 / 重载配置 / 重启 |
| 📒 **账本** | 逐日「已观测消费」，SD 卡 `/dswhale/ledger.json`，原子写入；**按 API key 指纹分本**（换 key 不丢历史） |
| 🔊 **音效** | 内置合成音（按键 / 完成 / 报错），不打包任何第三方素材；想用自定义音效就把 wav 放进 SD 卡 |
| 🗂️ **配置** | SD 卡 `/dswhale/config.json` 为主，**没插卡自动退回 NVS**，拔卡也能跑 |
| 🔐 **TLS** | 内置 DigiCert Global Root G2 根证书校验证书；需要中间人代理时可关（见配置文档） |

## 硬件与开发环境

- **M5Stack Cardputer ADV**（ESP32-S3，1.14" 240×135 IPS，TCA8418 键盘，8MB PSRAM）
- Arduino IDE 2.x 或 arduino-cli，**arduino-esp32 core 3.3.x**
- 库：`M5Cardputer ≥ 1.1.1`、`M5Unified ≥ 0.2.25`、`M5GFX ≥ 0.2.32`、`ArduinoJson ≥ 7.x`
- 一张 microSD 卡（可选，但强烈建议：配置和账本都放这儿）

> 老款 Cardputer（非 ADV）也能编译通过——M5Cardputer 1.1.1 会按板型自动切换键盘驱动（ADV 走 TCA8418）。但本工程只在 ADV 上验证。

## 快速开始

### 1. 开发板设置（Arduino IDE → 工具）

| 选项 | 值 |
|---|---|
| Board | **M5Cardputer** |
| PSRAM | **QSPI PSRAM**（离屏缓冲放 PSRAM，省内部 RAM） |
| Partition Scheme | **8M with spiffs (3MB APP/1.5MB SPIFFS)** |
| USB CDC On Boot | Enabled |

### 2. 命令行编译 / 上传

```bash
# 编译
arduino-cli compile \
  --fqbn "esp32:esp32:m5stack_cardputer:PSRAM=enabled,PartitionScheme=default_8MB" \
  DeepSeekWhale

# 上传（端口按实际情况改）
arduino-cli upload \
  --fqbn "esp32:esp32:m5stack_cardputer:PSRAM=enabled,PartitionScheme=default_8MB" \
  -p /dev/cu.usbmodem1101 \
  DeepSeekWhale
```

### 3. 配置 WiFi 与 API key

把 SD 卡插到电脑上，新建 `/dswhale/config.json`：

```json
{
  "wifi": { "ssid": "你的WiFi", "pass": "你的密码" },
  "api_key": "sk-你的DeepSeek密钥",
  "refresh_sec": 60
}
```

完整字段见 [docs/CONFIG.md](docs/CONFIG.md)。第一次开机时固件也会在 SD 卡上自动写一份模板。

API key 在 <https://platform.deepseek.com/api_keys> 申请；余额接口**只读**，不会产生任何调用费用。

### 4. 开机

1. 插卡 → 上电
2. 自动挂载 SD → 连 WiFi → NTP 对时 → 拉一次余额
3. 看到鲸鱼和余额就成功了

> 没看到余额？按 `TAB` 进菜单 → `Network`，那一屏能直接看到 HTTP 状态码、错误原因、IP、对时状态。

## 按键

| 按键 | 作用 |
|---|---|
| `ENTER` / `空格` | 主屏：鲸鱼冒泡；菜单：确认 |
| `TAB` | 打开 / 关闭菜单 |
| `` ` ``（或 `DEL`） | 返回上一层 |
| `;` `w` / `.` `s` | 上 / 下（ADV 键盘上也可用 `FN` + `;` `.` `,` `/` 的方向键），长按连发 |
| `,` `a` / `/` `d` | 左 / 右（设置项调值） |
| `R` | 立即刷新余额 |

## 目录结构

```text
DeepSeekWhale/
├── DeepSeekWhale.ino          # 主程序：状态机、按键、刷新调度
├── src/
│   ├── app_config.{h,cpp}     # SD / NVS 配置存储
│   ├── ledger.{h,cpp}         # 记账内核（accounting.mjs 的移植）
│   ├── pricing.{h,cpp}        # 定价表 + 峰谷时段 + 法定节假日
│   ├── net_link.{h,cpp}       # WiFi 状态机 + NTP + 余额接口
│   ├── ui.{h,cpp}             # 240x135 界面（主屏 / 菜单 / 账本 / 气泡）
│   ├── bubbles.{h,cpp}        # 鲸鱼台词与点击序列
│   ├── sound.{h,cpp}          # 合成音 + SD 上的自定义 wav
│   ├── money.h                # 定点金额工具（1e-8 元）
│   ├── root_ca.h              # DigiCert Global Root G2
│   └── assets/whale_96.h      # 生成的 96x96 RGB565 位图
├── assets/                    # 原始位图 bin（as-is，见 NOTICE.md）
├── tools/                     # bin2header / bin2png / 节假日与证书自检
├── docs/                      # 上手、配置、架构、移植对照
└── .github/workflows/ci.yml   # GitHub Actions：arduino-cli 编译
```

## 文档

- [上手与排错](docs/GETTING-STARTED.md)
- [配置文件字段](docs/CONFIG.md)
- [架构与数据流](docs/ARCHITECTURE.md)
- [与上游插件的功能对照](docs/PORTING.md)

## 已知限制

- **设备端拿不到 DSH 的会话事件**，所以「今日已用」是**余额观测口径**（和上游的「已观测消费」同源），没有逐轮 token 明细；余额接口不提供充值流水，充值与消费撞在同一次刷新间隔时会失真（上游同样如此）。
- 余额接口 60 秒轮询，**一天约 1440 次请求**；介意的话把 `refresh_sec` 调大。
- 没有做设备端**文本输入**：WiFi 密码和 API key 需要在电脑上写进 SD 卡（同理，也没有 AP 配网门户）。
- 上游的 mp3/wav/gif 素材不在 MIT 范围内，本工程**不打包**，只用合成音；想要原声请自备 wav 放 SD 卡。
- 峰谷节假日表内置到 **2026 年**；2027 年的放假安排公布后需要更新 `src/pricing.cpp`（`tools/check-holidays.py` 会提醒）。

## 许可证

本工程代码：[MIT](LICENSE)。

上游 `MeteorNOX/DeepSeek-Balance-Whale-Widget`（MIT）：记账算法、定价表、峰谷规则、鲸鱼形象均移植/衍生自该项目。`assets/` 下的美术素材沿用上游的 **as-is** 声明，**不适用 MIT**——细节见 [NOTICE.md](NOTICE.md)。
