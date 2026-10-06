# 变更日志

本文件记录本工程（M5Stack Cardputer ADV 固件）的变更。
格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

> 上游插件自己的版本线（`MeteorNOX/DeepSeek-Balance-Whale-Widget`，如 `0.3.18`）与本工程**无关**，
> 不要混用。本工程的版本号只描述这个固件。

## [Unreleased]

### 计划中

- 余额预警 / 今日预算（阈值放 `config.json`，超限冒泡 + 响铃）
- 余额校正（对齐上游 `reconcileBalance` 的扣除口径）
- 设备端配置编辑界面（不再必须拔卡改 JSON）
- AP 配网门户（手机填 WiFi 与 API key）
- 主机侧单元测试补 `ledger`（需要给 `SD`/`File` 写内存桩；`money` / `pricing` 已有）

## [1.2.1] - 2026-10-06

### 改进

- **旋转时鲸鱼不再带一个黑方块**：位图背景是纯黑，改成按「黑=透明键」贴图。
  先验证过图里**没有被内容完全包围的纯黑像素**（0 个），所以不会在鲸鱼身上打洞；
  静态与旋转两条路径都走同一个透明键，观感一致。
- **空闲时不再 5fps 全屏重绘**：以前内容没变也每秒推 5 次整屏，既费电又更容易
  看出撕裂。现在时钟每秒（关掉秒显示则每分钟）跳一次时才重绘，按键/数据变化
  与旋转动画仍然即时（动画期间 ~33fps）。

## [1.2.0] - 2026-10-06

### 修复

- **气泡一直闪烁**：`drawBubble()` 内部会先画主屏，而画主屏的函数结尾就 `pushSprite()`
  了一次 —— 等于每帧推两次屏：先显示「没有气泡的主屏」，再显示「有气泡的主屏」，
  5fps 下就是肉眼可见的闪。现在把主屏拆成 `composeMain()`（只画进离屏缓冲、不推屏）
  与 `drawMain()`（画完推一次），气泡在缓冲里盖上去后**只推一次**。
- **提示条（toast）同样闪烁**：改成 `setToast()` + 各屏在推屏前统一画一次（`drawToastPanel()`），
  不再「先推底图再推提示」。
- **按键事件可能漏报**：原来只在 `Keyboard.isChange()` 为真时做边沿检测，而
  `isChange()` 比较的是**按键数量**——同时按下/松开时数量不变，事件就丢了。
  现在每帧都做边沿检测（字符键与上一帧求差集），并给 `ENTER`/`TAB`/退格
  加了 120ms 最小间隔（一次物理按下 = 一次动作）。

### 新增

- **点按鲸鱼的 360° 打转动画**：420ms 转满一圈，动画期间刷新率提到 ~33fps；
  旋转时同时缩到 0.68 倍，否则 96×96 转到 45° 会戳出 100px 的画框。
  可在 `config.json` 里设 `"whale_spin": false`，或菜单 → `设置 → 鲸鱼旋转`。
- **点鲸鱼的独立音效**：`sound::whaleClick()`，和普通按键音区分开；
  支持 SD 卡上的自定义音效（**存在就用，不存在回落合成音**）：
  `/dswhale/sound/` 下的 `press.wav`、`release.wav`、`click.wav`、`task_end.wav`。
  上游插件的 mp3 素材不在 MIT 范围内、设备端也没有 mp3 解码器，需要自己用 ffmpeg
  转成 16bit PCM wav 放进去（命令见 docs/CONFIG.md）。

## [Unreleased]

### 变更

- **点按鲸鱼的动画由「360° 打转」改成「回弹」（Q 弹）**：底部中点固定，先用
  1.02×0.84 压扁、再 0.98×1.015 弹起过冲一点、最后 1.005×0.975 小回弹稳定，
  全程 320ms。
  - 关键帧刻意压在 `sx ≤ 1.02`、`sy ≤ 1.015`：鲸鱼画在 (5,20)、画框是
    18..118，`sy=1.015` 时顶端 18.56，还剩约 0.5px 余量——再大就会戳出画框。
  - 重新用上 `pushImageRotateZoom` 的 `zoom_x`/`zoom_y`（非等比缩放）+
    底部枢轴，角度固定 0，**不再旋转**。
  - 配置键与设置项同步改名：`whale_spin` → `whale_bounce`、`鲸鱼旋转` →
    `鲸鱼回弹`；旧的 `whale_spin` 仍然认（读到就用），不用改卡里的配置。

### 文档

- 补上 **SD 卡格式化要求**：必须是 **FAT32 + MBR 分区表**。
  依据是这份工具链里 FatFs 的编译配置：`FF_FS_EXFAT=0`（不认 exFAT）、
  `FF_LBA64=0`（只认 MBR，不认 GPT）。同时写清了 macOS/Windows/Linux
  各自的格式化命令，以及「卡要在开机前插好」（固件只在 setup 里挂载一次）
  和写保护开关这两个坑。

## [1.1.0] - 2026-10-06

### 新增

- **中英双语界面**：所有界面文案（主屏 / 菜单 / 账本 / 网络诊断 / 设置 / 关于 / 冒泡 / 提示 /
  启动页 / 报错）都做成中英双语。
  - 切换方式：菜单 → `设置 → 语言`，或主屏直接按 `L`；保存在 `config.json` 的 `lang` 字段
    （`"en"` / `"zh"`，默认 `en`），没插卡时存 NVS。
  - 文案集中在 `src/lang.h` 的 `DSW_STRINGS` X-macro 表里：**一行一个 key + 两种语言**，
    枚举与两张表同源，`static_assert` 保证不会漏翻。
  - 字体跟随语言：中文用 M5GFX 自带的 `efontCN_12/16/24`（U8g2 子集字库），
    英文继续用 `Font0` / `AsciiFont8x16` / `FreeSans9pt7b` / `Orbitron_Light_24`。
  - UI 不再有写死的行高：菜单、列表、设置的可视行数与气泡高度都按当前字体实测，
    中英各用各的排版（中文行高更大，可视行数自动变少）。
  - 中文长句（冒泡台词）按像素宽度**折行**，UTF-8 按首字符整字切分，不会把汉字劈开。

### 修复

- **英文文案里的 `·` 渲染不出来**：`observed spend · N days`、`account · N day(s)`、
  `MIT · upstream MIT` 用的是 ASCII 点阵字体，U+00B7 不在字库里 → 现在改成 `|` 与 `/`。
  这个 bug 在 1.0.0 就存在，是被新增的 `tools/check-cjk-font.py` 抓出来的。

### 工程

- 新增 `tools/check-cjk-font.py`：直接解析 M5GFX 里 `lgfx_efont_cn.c` 的 U8g2 unicode 查找表，
  静态校验「英文文案纯 ASCII + 中文字形全部命中字库」，CI 里跑。
- 编译体积：**2,479,375 B / 3,342,336 B（74%）**，其中中文字库 +1.08MB
  （12/16/24 三档分别是 +215KB / +318KB / +551KB）。`8M with spiffs (3MB APP)` 分区仍然够用。

## [1.0.0] - 2026-10-06

首个版本：把上游 DSH 挂件的核心能力搬到 Cardputer ADV。

### 新增

- **余额**：`GET https://api.deepseek.com/user/balance`，显示总余额 / 赠金 / 充值余额；
  默认 60 秒自动刷新（`refresh_sec` 可调，15–900 秒），支持手动刷新（菜单 `Refresh now` 或快捷键 `R`）。
- **今日已用（记账内核）**：移植上游 `lib/accounting.mjs` 的口径——
  余额下降累计为消费、上升单独记为充值/赠金、`1e-8` 元定点运算、按北京时间分日、
  `partialDay`（10 分钟观测缺口）判定；网络抖动时沿用最近一次余额不报错。
- **按密钥指纹分本**：`FNV-1a 64` 指纹作为记账本 key，换 API key 等于换一本账，旧本保留、不跨本相加。
- **峰谷定价与倒计时**：工作日 `9:00–12:00` / `14:00–18:00` 高峰（双倍价）；
  周末（2026-08-23 起）与法定节假日（2026-09-19 起）全天谷价；
  `nextPeakChangeAt()` 与 `isPeakTime()` 同源，主屏显示下一切换点倒计时。
- **鲸鱼 UI**：96×96 RGB565 位图（由上游 `DSniang1.png` 处理而来）+ 状态栏 + 数据列 +
  提示行，整屏离屏渲染后一次 `pushSprite`（16bit sprite 放 PSRAM）。
- **冒泡序列**：点按鲸鱼 / `ENTER` 依次显示 余额 → 今日已用 → 峰谷 → 随机台词，
  30 秒不点回到第 1 项；随机台词带权重且不连续重复；`bubble_auto_close_sec` 控制自动关闭。
- **键盘菜单**：刷新 / 冒泡 / 账本 / 网络 / 设置 / 关于 / 重载配置 / 重启；
  `TAB` 开关菜单、`` ` ``（或 `DEL`）返回、方向键支持长按连发。
- **账本界面**：逐日「已观测消费」列表（最近 60 天），显示记账本数量。
- **网络诊断屏**：状态、SSID、IP、RSSI、对时、HTTP 码、延迟、币种、可用性、赠金、账本等一屏看全。
- **设置界面**：亮度 / 音效 / 音量 / 刷新间隔 / TLS 校验 / 气泡秒数 / 时钟秒，改动 2.5 秒后自动落盘。
- **存储**：`/dswhale/config.json`（SD 优先）+ NVS 兜底；`/dswhale/ledger.json` 原子写入
  （`.tmp` → `remove` → `rename`）。
- **TLS**：内置**两个**根证书校验 `api.deepseek.com`——DigiCert Global Root G2（中国大陆实测链路）
  与 Amazon Root CA 1（海外实测链路）；只钉一个会让另一边的用户握手失败。
  NTP 未同步或需要中间人代理时自动/手动降级。
- **音效**：运行期合成（按键 / 完成 / 报错），支持 SD 上的 `task_end.wav` 覆盖；**不打包**任何第三方素材。
- **工具**：`tools/bin2header.py`（裸位图 → C 头）、`tools/bin2png.py`（裸位图 → PNG 预览）、
  `tools/check-holidays.py`（节假日表覆盖自检）、`tools/check-cert-chain.sh`（证书链复核）。
- **测试**：主机侧单元测试 `tests/`（`bash tests/run.sh`，秒级、不需要硬件）——
  `money` 定点金额、`pricing` 北京时间换算与价目表，外加**与上游实现对拍**：
  `tests/golden/peak-2026.tsv` 由 `tests/gen-golden.mjs`（转录上游 `lib/index.js` 的
  `isPeakTime` / `nextPeakChangeAt`）生成，覆盖 396 天 / 9504 个小时样本 / 396 个切换点。
- **CI**：GitHub Actions 分三段——节假日/证书/生成物一致性自检、主机侧单元测试、
  以及在 `PSRAM=enabled` 与 `PSRAM=disabled` 两种配置下各编译一遍；编译产物作为 artifact 上传。

### 说明

- 固件约 **1.39 MB**，**必须**把 Partition Scheme 设成 `8M with spiffs (3MB APP)` 或更大；
  默认 4MB 分区（1.2MB APP）装不下。
- 「今日已用」是**余额观测口径**，不含逐轮 token 明细（设备端看不到 DSH 会话事件）。
- 节假日表只覆盖到 **2026 年**；2027 年安排公布后需更新 `firmware/DeepSeekWhale/src/pricing.cpp`（`tools/check-holidays.py` 会提醒）。

### 开发过程中被测试/CI 抓出来的坑（留个记录）

- **DeepSeek 国内外证书链不同**：本机（大陆）是 `TrustAsia ← DigiCert Global Root G2`，
  GitHub Actions 美国 runner 上却是 `Amazon RSA 2048 M01 ← Amazon Root CA 1`。
  只钉一个根会让另一边的用户握手失败 → 现在两个根都内置（`tools/certs/`）。
- **模型名匹配漏了大小写归一**：上游 `priceFor()` 第一步是 `toLowerCase()`，
  移植时漏了 → 单测里加了一条 `DeepSeek-V4-Pro` 的用例锁住行为。

[Unreleased]: https://github.com/seaison/DeepSeek-Whale-Cardputer/compare/v1.2.1...HEAD
[1.2.1]: https://github.com/seaison/DeepSeek-Whale-Cardputer/releases/tag/v1.2.1
[1.2.0]: https://github.com/seaison/DeepSeek-Whale-Cardputer/releases/tag/v1.2.0
[1.1.0]: https://github.com/seaison/DeepSeek-Whale-Cardputer/releases/tag/v1.1.0
[1.0.0]: https://github.com/seaison/DeepSeek-Whale-Cardputer/releases/tag/v1.0.0
