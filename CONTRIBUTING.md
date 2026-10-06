# 贡献指南

欢迎 PR / issue。下面是把事情做顺的最短路径。

## 一、先读这两份

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)：模块划分、主循环、数据流
- [docs/PORTING.md](docs/PORTING.md)：哪些上游能力搬过来了、哪些搬不动、为什么

**口径问题（余额怎么算、峰谷怎么判、定价表）以上游和 DeepSeek 官方文档为准**，
不要在本工程里"发明"新规则——本工程的价值就是和上游保持一致。

## 二、本地构建

```bash
arduino-cli core install esp32:esp32@3.3.10
arduino-cli lib install M5Cardputer@1.1.1 M5Unified@0.2.25 M5GFX@0.2.32 ArduinoJson@7.4.3

arduino-cli compile \
  --fqbn "esp32:esp32:m5stack_cardputer:PSRAM=enabled,PartitionScheme=default_8MB" \
  --warnings all .
```

提交前请至少跑一遍编译和单元测试（CI 跑的就是这两样）：

```bash
bash tests/run.sh    # money / pricing 纯逻辑 + 与上游实现对拍（秒级，不需要硬件）
```

改动涉及 `assets/` 或生成的位图头时，还要跑：

```bash
python3 tools/bin2header.py --input assets/whale_96x96_rgb565.bin \
  --output firmware/DeepSeekWhale/src/assets/whale_96.h --width 96 --height 96 --name whale_96 --format rgb565
git diff --exit-code firmware/DeepSeekWhale/src/assets/whale_96.h   # 应该没有差异
```

## 三、依赖版本

`M5Cardputer` 必须是 **≥ 1.1.1**——只有它按板型自动切换键盘驱动（ADV → TCA8418）。
升级依赖时请同时改三处，保持一致：

1. `.github/workflows/ci.yml` 的 `ARDUINO_LIBS` / `ARDUINO_CORE`
2. `README.md`「硬件与开发环境」
3. `docs/GETTING-STARTED.md`「准备」

## 四、代码风格

- 4 空格缩进，LF 换行（`.editorconfig` / `.gitattributes` 已配）
- 文件名与命名空间：`snake_case` 文件、`camelCase` 函数、`kConstant` 常量、成员 `xxx_`
- 中文注释写**为什么**，英文标识符写**是什么**；对齐口径的地方请注明上游对应位置
  （例如 `// 与上游 partialDayOf() 同源`）
- 不要用 `String`（除个别必须的边界），配置与账本一律 `std::string`；金额一律 `int64`（`money.h`）
- 不要在 `loop()` 里放 `delay()`（开机流程除外），需要等就用状态机
- 新增可调参数请同时更新：`AppConfig` 字段、`app_config.cpp` 读写、`settingLabels()`、`docs/CONFIG.md`

## 五、提交信息

用 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/)：

```
feat(ui): 主屏加峰谷倒计时进度条
fix(ledger): 修正跨天样本被误判为乱序
docs(porting): 补充「每轮消耗为什么搬不动」
chore(ci): 缓存 esp32 core
```

一个 PR 一件事。改了行为请在 `CHANGELOG.md` 的 `Unreleased` 加一条。

改了 `pricing.cpp` 的峰谷/定价逻辑、或改了 `money.h` 时，记得同步 `tests/native/run_tests.cpp`
里的用例；改了峰谷规则还要重新生成 golden（见 [tests/README.md](tests/README.md)）。

## 六、真机验证

改界面、键盘、网络、存储的 PR **必须**在真机上试过，并在 PR 描述里写清楚：

- 设备（ADV / 老款 Cardputer）
- 开发板设置（PSRAM、Partition Scheme）
- 试了哪些屏、哪些按键
- `Network` 屏或串口的关键输出（**API key 请打码**）

只改文档、注释、CI 的 PR 不需要真机。

## 七、几类特殊改动的注意事项

### 文案与中文字库

界面文案集中在 `firmware/DeepSeekWhale/src/lang.h` 的 `DSW_STRINGS(X)` 表，
**一行一个 key + 英文 + 中文**；加文案就往里加一行，渲染代码不用动：

```cpp
X(Balance, "BALANCE", "余额")   // 枚举/英文表/中文表都由这一行生成
```

两条硬规则（CI 里由 `tools/check-cjk-font.py` 检查）：

1. **英文文案只能出现 ASCII**——英文界面用的是点阵/矢量字体，`·`、`—`、`“”` 这类
   非 ASCII 字符会渲染成空白（1.0.0 就踩过 `·` 这个坑）。
2. **中文字必须是 `efontCN_*` 里有的字**——那是 U8g2 子集字库（约 7428 字形），不是全字集。
   检查器会把缺字连所在行一起报出来，改文案或者换表达即可。

鲸鱼台词在 `lang.cpp` 的 `kBubblePairs`，一行为 `{权重, 英文, 中文}`，中英要一一对应。

### 需要维护的两张表

| 表 | 位置 | 什么时候要动 |
|---|---|---|
| 法定节假日 | `firmware/DeepSeekWhale/src/pricing.cpp` 的 `kHolidayValley[]` | 每年 11 月国务院发布次年安排后（`tools/check-holidays.py` 会提醒，CI 会跑） |
| 价目表 | `firmware/DeepSeekWhale/src/pricing.cpp` 的 `kFlash` / `kPro` | DeepSeek 官方调价时（上游 `lib/index.js` 的 `PRICING` 是同一份数据） |

### 证书

`firmware/DeepSeekWhale/src/root_ca.h` 由 `tools/make-root-ca.py` 从 `tools/certs/*.pem` 生成（**不要手改**），目前内置两个根：
DigiCert Global Root G2 与 Amazon Root CA 1（有效期都到 2038）。

```bash
bash tools/check-cert-chain.sh          # 联网复核链根是否命中内置根（CI 会跑）
python3 tools/make-root-ca.py --check   # 校验 root_ca.h 与 tools/certs/ 一致（CI 会跑）
```

发现链根变了（例如 DeepSeek 换了 CDN）：把新 PEM 放进 `tools/certs/`，跑一次 `make-root-ca.py`，
提交时把新 PEM 和重新生成的头文件一起带上。

### 素材与许可

`assets/` 下的位图**不适用 MIT**，沿用上游的 as-is 声明（见 [NOTICE.md](NOTICE.md)）。
**不要**把上游的 mp3 / wav / gif 加进仓库——那些素材明确不在 MIT 范围内。
需要音效就往 `sound.cpp` 里加合成音，或者让用户自己放 SD 卡。

### 版本号与发布

版本号在 `firmware/DeepSeekWhale/DeepSeekWhale.ino` 的 `APP_VERSION`，改完要同步 `CHANGELOG.md`：

```bash
git tag -a v1.0.0 -m "DeepSeek Whale 1.0.0"
git push origin v1.0.0
gh release create v1.0.0 --title "v1.0.0" --notes "见 CHANGELOG.md"
```

## 八、上游同步

上游仍在活跃开发（记账修复、新厂商模板、安全边界收紧…）。与本工程有关的部分：

- **定价表 / 峰谷规则 / 节假日**：直接对齐 `lib/index.js`
- **记账内核**：对齐 `lib/accounting.mjs`
- **时长**：上游是 Node.js，本工程是 C++，不要试图逐行翻译；对齐**口径**即可，注释里写清对应函数名
