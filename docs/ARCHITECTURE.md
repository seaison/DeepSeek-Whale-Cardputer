# 架构与数据流

## 一、模块图

```text
                ┌──────────────────────────────────────────┐
                │            DeepSeekWhale.ino             │
                │  状态机(Screen) · 按键路由 · 刷新调度     │
                └───┬───────┬────────┬────────┬────────┬────┘
                    │       │        │        │        │
        ┌───────────▼──┐ ┌──▼─────┐ ┌▼──────┐ ┌▼─────┐ ┌▼────────┐
        │ app_config   │ │net_link│ │ledger │ │  ui  │ │ sound   │
        │ SD / NVS     │ │WiFi+NTP│ │记账内核│ │240x135│ │合成音   │
        └──────────────┘ │balance │ └───┬───┘ └──────┘ └─────────┘
                         └───┬────┘     │
                    ┌────────▼───┐ ┌────▼─────┐
                    │  money.h   │ │pricing   │
                    │ 定点 1e-8  │ │峰谷+节假日│
                    └────────────┘ └──────────┘
```

依赖是单向的：`pricing` / `money` 是纯函数模块（可单独测），`ledger` 依赖它们，`ui` 只读 `ViewModel`（不知道业务），`.ino` 负责把所有东西接起来。

## 二、主循环

```cpp
loop() {
    M5Cardputer.update();   // 键盘扫描 + 显示刷新（M5Unified）
    g_net.loop();           // 非阻塞 WiFi 状态机（3s→30s 退避重连）
    sound::loop();          // 推进合成音序列
    pollKeys();             // 边沿检测 + 长按连发 → Screen 路由
    periodicFetch();        // 每 refresh_sec 拉一次余额
    debouncedSave();        // 配置改动 2.5s 后落盘
    saveLedger(false);      // 账本节流保存（≥30s 一次）
    render();               // 最多 5fps 重绘（forceRender 立即生效）
    delay(5);
}
```

**没有任何 `delay` 阻塞 UI**，除了开机阶段的等待（`waitWifi` / `waitTime` 里用 `M5Cardputer.update()` + `delay(50)` 保证进度能画出来）。余额请求是同步阻塞的（约 0.3–2 秒），期间主屏显示 `......`。

## 三、状态机

```text
Boot ──► Main ──TAB──► Menu ──┬──► Ledger  ──`──┐
          ▲  │                ├──► Net     ──`──┤
          │  │                ├──► Settings ─`──┤
          │  │                ├──► About   ──`──┤
          │  └──`─────────────┴──────────────────┘
          │
     ENTER/空格 → 气泡（叠加在主屏上，不进状态机）
```

气泡是**叠加层**不是独立屏幕：`drawBubble()` 会先画一遍主屏再盖面板，这样鲸鱼始终在画面里，和上游挂件的观感一致。

## 四、记账数据流

```text
  每 refresh_sec
       │
       ▼
  GET /user/balance ──► BalanceSnapshot{total, granted, toppedUp, at}
       │                                   │
       │  失败：保留上次余额，只更新错误行    │ 成功
       ▼                                   ▼
  lastError 上屏                 ledger.observe(at, total, currency)
                                           │
                        ┌──────────────────┴────────────────┐
                        │ 按北京时间取当日 DayRow            │
                        │ delta = 上次余额 − 本次余额        │
                        │ delta > 0 → debit += delta（消费） │
                        │ delta < 0 → credit += −delta（充值）│
                        │ 乱序/重复样本（at ≤ book.lastAt）丢弃│
                        └──────────────────┬────────────────┘
                                           ▼
                              /dswhale/ledger.json（原子写、节流）
```

关键点：

- **金额全是 `int64` 的 1e-8 元**（`money.h`），不做浮点累加；只在最后格式化时转 `double`。
- **消费与充值分开记**，充值不会冲掉已有消费（上游 0.3.1 的核心修复，本工程沿用）。
- **按密钥指纹分本**：`scopeFromKey()` = FNV-1a 64 的 16 位十六进制。换 key = 换一本账，旧本留在文件里不删、也不和当前本相加。
- **partialDay**：当天第一次观测离 00:00 超过 10 分钟（或对已过去的日子，最后一次观测离次日 00:00 超过 10 分钟）就标记为「部分数据」。上游用这个标记提示「数字偏小是正常的」，本工程把部分数据用淡蓝色显示。

## 五、峰谷判定

`pricing::isPeakTime(utcSec)` 是唯一判据，`nextPeakChangeAt()` 和 `estimateCostUnits()` 都调它，保证三者永不打架：

```text
北京时间(UTC+8)
  ├─ 周末（周六/周日，2026-08-23 起生效）────► 谷价
  ├─ 在 HOLIDAY_VALLEY 表里（2026-09-19 起）──► 谷价
  ├─ 9:00–12:00 或 14:00–18:00 的工作日 ─────► 高峰价（×2）
  └─ 其余 ────────────────────────────────► 谷价
```

`nextPeakChangeAt()` 从当前时刻起，按北京时间 0/9/12/14/18 点这些边界往后扫 12 天（最长的春节假期是 9 天，留足余量），返回第一个「档位发生变化」的时刻。

## 六、内存与空间预算

实测（arduino-esp32 3.3.10，PSRAM 开启，8MB 分区）：

| 项 | 占用 |
|---|---|
| Flash | **2,479,375 B / 3,342,336 B（74%）** |
| 静态 DRAM | **52,184 B / 327,680 B（15%）** |
| 离屏缓冲 240×135×16bit | 64,800 B（放 PSRAM） |
| TLS 握手瞬时堆 | ~35–45 KB |

鲸鱼位图（9216 像素 × 2B = 18 KB）作为 `const` 数组直接在 flash 里，不占 RAM。

其中中文字库是大头，按字号实测（arduino-cli 编译差值）：

| 字库 | 增量 |
|---|---|
| `efontCN_12`（小标签） | +215 KB |
| `efontCN_16`（菜单 / 列表） | +318 KB |
| `efontCN_24`（主数值 / 标题） | +551 KB |
| **合计** | **+1.08 MB** |

> ⚠️ 因为固件约 2.48MB（其中中文占 1.08MB），**默认的 4MB 分区（1.2MB APP）装不下**，必须选 `8M with spiffs (3MB APP)` 或更大。
> 想把中文去掉省空间：删掉 `lang.cpp` 里的 `kFontsZh` 与 `lang.h` 的 `Zh` 分支即可（英文界面不依赖 efont）。

## 七、中英双语怎么做的

文案集中在一处：`src/lang.h` 的 `DSW_STRINGS` 宏表，**一行一个 key + 英文 + 中文**，
枚举、英文表、中文表都由这一份列表生成（X-macro），所以

- 漏翻不可能悄悄发生：`static_assert` 保证两张表长度等于枚举数；
- 加一条文案 = 加一行，不需要动任何渲染代码。

```cpp
#define DSW_STRINGS(X)                                     \
    X(Balance, "BALANCE", "余额")                           \
    X(TodayUsed, "TODAY USED", "今日已用")                   \
    ...                                                    \
X 展开成三种东西：enum class Str、kEn[]、kZh[]
```

字体也跟着语言走（`lang::fonts()` 返回一个 `FontSet`）：

| | small（标签） | mono（菜单/列表） | value（主数值） | title（启动页） |
|---|---|---|---|---|
| 英文 | `Font0` 6x8 | `AsciiFont8x16` | `FreeSans9pt7b` | `Orbitron_Light_24` |
| 中文 | `efontCN_12` | `efontCN_16` | `efontCN_24` | `efontCN_24` |

所以 **UI 里没有任何写死的行高**：菜单/列表/设置的可视行数、气泡高度、状态条文案宽度
都是先 `setFont()` 再 `fontHeight()/textWidth()` 实测出来的，中英各用各的。

两个由 CI 兜住的坑（见 `tools/check-cjk-font.py`）：

1. **英文文案必须是纯 ASCII**。英文界面用的是点阵/矢量字体，出现 `·`（U+00B7）这类字符
   会渲染成空白 —— 这个 bug 在加双语之前就存在（`observed spend · 3 days`），是被这个检查抓出来的。
2. **中文的每个字都要在字库里**。`efontCN_*` 是 U8g2 子集字体（约 7428 字形），不是全字集。
   检查器直接从 `M5GFX/src/lgfx/Fonts/efont/lgfx_efont_cn.c` 里解出 U8g2 的 unicode 查找表，
   逐字核对（解析规则与 M5GFX `U8g2font::getGlyph()` 一致：记录里的长度字节是**整条记录**的字节数）。

鲸鱼台词同理：`lang.cpp` 的 `kBubblePairs` 一行为 `{权重, 英文, 中文}`，中英一一对应。

## 八、可测性

纯函数模块可以脱离硬件验证：

```bash
# 节假日表是否覆盖了需要的年份（CI 里会跑）
python3 tools/check-holidays.py

# api.deepseek.com 的证书链是否仍由内置根签发
bash tools/check-cert-chain.sh
```

`money.h` 与 `pricing.cpp` **完全不依赖 Arduino**，所以 `tests/native/run_tests.cpp` 直接用
主机编译器（`g++`/`clang++`）编译它们并断言行为——`bash tests/run.sh` 秒级跑完，
CI 里是一个独立的 job：

- `money`：定点金额解析/格式化（含进位、负数、非法输入）
- `pricing`：北京时间换算、跨零点、星期几、闰日
- `pricing`：价目表与大小写不敏感的模型名匹配
- `pricing`：**与上游实现对拍**——`tests/golden/peak-2026.tsv` 是由
  `tests/gen-golden.mjs`（逐行转录上游 `lib/index.js` 的 `isPeakTime` / `nextPeakChangeAt`）
  生成的，覆盖 2026-01-01 ~ 2027-01-31 共 396 天、9504 个小时样本、396 个切换点。
  这样验证的是「移植与上游等价」，而不是「自己和自己一致」。

`ledger.cpp` 因为依赖 `SD`/`File`，目前还没有主机侧测试（见 [tests/README.md](../tests/README.md) 第四节）。
