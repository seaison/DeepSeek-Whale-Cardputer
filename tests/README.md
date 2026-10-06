# 测试

两层：**主机侧单元测试**（秒级、无需硬件）和 **GitHub Actions 的编译矩阵**。

## 一、主机侧单元测试

```bash
bash tests/run.sh
```

不需要 ESP32、不需要 arduino-cli，只用 `g++`/`clang++` 编译 `firmware/DeepSeekWhale/src/pricing.cpp` + `tests/native/run_tests.cpp`
（这两个文件刻意不依赖 Arduino：`money.h` 只用 `<stdint.h>/<stdio.h>/<stdlib.h>`，
`pricing.cpp` 只用 `<math.h>/<string.h>/<time.h>`）。

覆盖：

| 组 | 内容 |
|---|---|
| `money` | `1e-8` 定点金额的解析/格式化：最小单位、负数、四舍五入进位（`9.999 → 10.00`）、非法输入 |
| `pricing` 时间 | 北京时间 ↔ epoch 互转、跨零点边界、星期几、闰日、未同步（epoch 0） |
| `pricing` 价目表 | Flash / Pro 价格、旧模型名回落、未知模型回落、**大小写不敏感**、按 token 估算金额 |
| `pricing` 对拍 | 拿 `tests/golden/peak-2026.tsv` 逐小时比对 `isPeakTime`，并比对每天的 `nextPeakChangeAt` |

最近一次运行（396 天 / 9504 个小时样本 / 396 个切换点）：

```
通过 50，失败 0
```

## 二、golden 数据是怎么来的

`tests/golden/peak-2026.tsv` **不是**由本工程的 C++ 实现生成的，而是由
[`tests/gen-golden.mjs`](gen-golden.mjs) 生成的——那个脚本里的
`isPeakTime` / `nextPeakChangeAt` 是**逐行转录自上游** `lib/index.js`（MIT）的原始实现。

这样对拍验证的才是「本移植与**上游**等价」，而不是「我自己的两段代码互相同意」。

格式（TSV）：

```
# <北京日期> <6 位十六进制> <下一切换时刻 epoch 秒 | 0>
2026-10-08	03ce00	1791594000
```

- 六位十六进制是当天 24 小时的位图：**第 h 位（LSB 起）为 1 = 该小时属高峰**；
- 第三列是 `nextPeakChangeAt(当日北京 00:00)`，`0` 表示 12 天内没有切换点。

覆盖 `2026-01-01` ~ `2027-01-31`，其中特意包含几段有意义的边界：

| 日期 | 为什么重要 |
|---|---|
| `2026-08-22` / `2026-08-23` | 周末谷价的生效分界（8/23 00:00 起） |
| `2026-09-19` | 法定节假日谷价的生效分界 |
| `2026-10-01` ~ `2026-10-07` | 国庆 7 天连假，且跨越工作日 |
| `2027-01-01` 起 | 节假日表**没有**覆盖 2027 —— 用来锁定「表外年份仍按工作日 9-12/14-18 判峰」的行为 |

### 重新生成

```bash
node tests/gen-golden.mjs > tests/golden/peak-2026.tsv
bash tests/run.sh
```

**什么时候要重新生成：** 上游改了峰谷规则、DeepSeek 官方改了时段，或者国务院公布了下一年的
放假安排（那时还要同步 `firmware/DeepSeekWhale/src/pricing.cpp` 的 `kHolidayValley` 与 `tools/gen-golden.mjs` 里的同一张表）。
CI 会检查 `tests/golden/peak-2026.tsv` 与生成器的输出一致，防止有人手改 golden。

## 三、CI 里的编译矩阵

`.github/workflows/ci.yml` 分三段：

1. **Policy checks**：节假日表覆盖、`api.deepseek.com` 证书链是否命中内置根、
   生成的位图头/根证书头是否与源文件一致、Python 工具语法；
2. **Native tests**：`bash tests/run.sh`（外加 golden 与生成器的一致性检查）；
3. **Compile**：`esp32:esp32:m5stack_cardputer` 下按 `PSRAM=enabled` 与 `PSRAM=disabled`
   两种开发板设置各编译一遍（`--warnings all`，本工程代码 0 告警），并上传固件 artifact。

## 四、没被自动测试覆盖的部分

| 模块 | 为什么 | 怎么手工验证 |
|---|---|---|
| `ledger.cpp` | 依赖 `SD` 与 ArduinoJson 的 `File` 接口，主机侧要写桩才好测 | 真机上插卡跑一天，看账本界面/SD 上的 `ledger.json` |
| `ui.cpp` | 纯渲染，只能看图 | 真机肉眼 |
| `net_link.cpp` | 需要真实网络与 API key | 菜单 → `Network` 屏看 HTTP 码与错误信息 |
| `app_config.cpp` / `sound.cpp` | 依赖硬件与文件系统 | 真机 |

欢迎给 `ledger.cpp` 补一个主机侧桩（把 `SD`/`File` 换成内存实现）——这是目前性价比最高的一块。
