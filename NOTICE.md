# 署名、来源与许可范围（NOTICE）

## 一、本工程是什么

**DeepSeek Whale · M5Stack Cardputer ADV** 是
[`MeteorNOX/DeepSeek-Balance-Whale-Widget`](https://github.com/MeteorNOX/DeepSeek-Balance-Whale-Widget)（MIT）
到 **M5Stack Cardputer ADV** 的**移植**。上游是 DSH（DeepSeek Harness）Web 界面右下角的余额挂件，本工程把它变成设备上的独立固件。

## 二、逐项来源

| 本工程内容 | 来源 | 许可 |
|---|---|---|
| `src/ledger.{h,cpp}` | 移植自上游 `lib/accounting.mjs`（余额观测记账、按密钥指纹分本、部分数据判定 `PARTIAL_GAP_MS`） | MIT |
| `src/pricing.{h,cpp}` | 移植自上游 `lib/index.js` 的 `PRICING` / `BASE_PRICE` / `PRO_PRICE` / `PEAK_HOURS` / `HOLIDAY_VALLEY` / `isPeakTime` / `nextPeakChangeAt` | MIT |
| `src/net_link.cpp` | 依据上游 `BALANCE_URL`（`https://api.deepseek.com/user/balance`）与字段路径 `balance_infos[0].total_balance` 等接口约定重写 | MIT |
| `src/money.h` | 对齐上游 `SCALE = 1e8` 的定点金额口径 | MIT |
| `src/bubbles.cpp` | 概念对应上游的「点击序列」（首次点击泡 → 队列 → 随机台词）；台词为**本工程重写**（英文） | MIT |
| `src/ui.{h,cpp}`、`DeepSeekWhale.ino`、`src/app_config.*`、`src/net_link.*`、`src/sound.*` | 本工程原创（设备端 UI / 配置 / 网络状态机 / 音效） | MIT |
| `assets/whale_96x96_rgb565.bin` | 由上游 `assets/DSniang1.png`（小鲸鱼本体）经使用者本地处理后得到的 96×96 RGB565 裸位图 | **不适用 MIT**：上游声明为「as-is，不授予再许可」 |
| `docs/images/whale_96.png` | 由上面那个 bin 渲染出的预览图 | 同上 |
| `src/assets/whale_96.h` | 由 `tools/bin2header.py` 从上面的 bin 生成 | 同上（位图数据的另一种编码） |

**本工程不包含**上游 `assets/` 下的 mp3 / wav / gif 素材，也不包含其 PNG 原图——那些素材不在 MIT 覆盖范围内。设备端音效改为**运行期合成**（`src/sound.cpp`）；想用上游的原声，请自行把 wav 放进 SD 卡（见 `docs/CONFIG.md`）。

## 三、上游对素材的声明（原文摘录）

上游 `PROVENANCE.md` 把代码与美术素材分开授权：

> `lib/`、`cordis.patch.yml`、`package.json`、文档与维护脚本 —— **MIT**
> `assets/**`（图片 / 动图 / 音效）—— **不适用 MIT**：由维护者提供或使用 AI 工具生成，按 **as-is** 随插件分发，仅用于运行本插件；不授予再许可，也不声明为原创作品。

本工程沿用同一划分：**代码 MIT，位图素材 as-is**。若素材权利人提出异议，会立即移除相关文件（见第六节）。

## 四、上游 MIT 许可全文

```
MIT License

Copyright (c) 2026 MeteorNOX

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 五、第三方组件

| 组件 | 许可 | 用途 |
|---|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) / [M5GFX](https://github.com/m5stack/M5GFX) / [M5Cardputer](https://github.com/m5stack/M5Cardputer) | MIT | 硬件抽象、显示、键盘 |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | MIT | 配置 / 账本 / 接口 JSON |
| arduino-esp32 | LGPL-2.1 | ESP32 平台核心 |
| DigiCert Global Root G2 / Amazon Root CA 1 证书 | 公共根证书（DigiCert / Amazon 发布） | `src/root_ca.h`（由 `tools/certs/*.pem` 生成），用于校验 `api.deepseek.com` 的证书链 |

## 六、权利主张

如果你是上游素材的权利人且不希望它出现在这里，请开 issue 或联系维护者，相关文件会被移除或替换。
