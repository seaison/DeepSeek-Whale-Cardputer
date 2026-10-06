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
| Flash | **1,388,567 B / 3,342,336 B（41%）** |
| 静态 DRAM | **52,008 B / 327,680 B（15%）** |
| 离屏缓冲 240×135×16bit | 64,800 B（放 PSRAM） |
| TLS 握手瞬时堆 | ~35–45 KB |

鲸鱼位图（9216 像素 × 2B = 18 KB）作为 `const` 数组直接在 flash 里，不占 RAM。

> ⚠️ 因为固件约 1.39MB，**默认的 4MB 分区（1.2MB APP）装不下**，必须选 `8M with spiffs (3MB APP)` 或更大。

## 七、可测性

纯函数模块可以脱离硬件验证：

```bash
# 节假日表是否覆盖了需要的年份（CI 里会跑）
python3 tools/check-holidays.py

# api.deepseek.com 的证书链是否仍由内置根签发
bash tools/check-cert-chain.sh
```

`money.h`、`pricing.cpp`、`ledger.cpp` 都不依赖 M5 硬件，只依赖 `<stdint.h>` / `<time.h>` / ArduinoJson（`ledger` 的序列化部分），在桌面编译器下加个 `SD` 的桩就能跑单元测试——目前还没做，属于 welcome contribution。
