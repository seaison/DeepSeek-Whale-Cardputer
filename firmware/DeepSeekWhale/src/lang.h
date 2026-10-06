// 界面语言（中 / 英）。
//
// 设计要点：
//   · 所有文案集中在本文件的 DSW_STRINGS 表里，**一行一个 key + 两种语言**，
//     加文案只改一行；漏翻会在编译期就暴露（枚举与两张表由同一张表生成）。
//   · 字体也跟着语言走：英文用 M5GFX 自带的点阵/矢量字体（紧凑好看），
//     中文必须用 efontCN_*（U8g2 子集字库，约 7545 字形）。
//     ⚠️ 中文那三档字号要给固件增加约 1.08MB flash，详见 docs/ARCHITECTURE.md。
//   · 中文文案用到的字是否在字库里，由 tools/check-cjk-font.py 静态检查（CI 会跑）。
#pragma once

#include <M5GFX.h>
#include <stdint.h>

enum class Lang : uint8_t { En = 0, Zh = 1 };

namespace lang {

// ===== 文案表（key, 英文, 中文）=====
// 用 X-macro：枚举、英文表、中文表都从这一份列表生成，三者不可能不一致。
#define DSW_STRINGS(X)                                                                          \
    /* —— 状态栏 / 徽标 —— */                                                                   \
    X(BadgePeak, "PEAK", "高峰")                                                                \
    X(BadgeOffPeak, "OFF-PEAK", "空闲")                                                         \
    X(BadgeSyncTime, "SYNC TIME", "对时中")                                                     \
    X(WifiNoConfig, "No WiFi", "未配网")                                                        \
    X(WifiConnecting, "WiFi...", "连网中")                                                      \
    X(WifiSignalFmt, "WiFi %ddBm", "WiFi %ddBm")                                                \
    X(ClockPending, "--:--", "--:--")                                                           \
    /* —— 主屏 —— */                                                                            \
    X(Balance, "BALANCE", "余额")                                                               \
    X(TodayUsed, "TODAY", "今日已用")                                                             \
    X(PeakPrice, "PEAK", "高峰计价")                                                              \
    X(OffPeakPrice, "OFF-PEAK", "空闲计价")                                                       \
    X(WaitingTime, "waiting for time", "等待对时")                                                \
    X(NoData, "--", "--")                                                                       \
    X(HintMain, "TAB menu  ENTER bubble  R refresh", "TAB 菜单  ENTER 冒泡  R 刷新")            \
    X(HintUpdatedFmt, "TAB menu  ENTER bubble  updated %s ago",                                 \
      "TAB 菜单  ENTER 冒泡  更新于 %s 前")                                                     \
    X(HintPartialToday, "TAB menu  ENTER bubble  partial data today",                           \
      "TAB 菜单  ENTER 冒泡  今日数据不全")                                                   \
    /* —— 菜单 —— */                                                                            \
    X(MenuTitle, "MENU", "菜单")                                                                \
    X(MenuRefresh, "Refresh now", "立即刷新")                                                   \
    X(MenuBubble, "Show bubble", "显示气泡")                                                    \
    X(MenuLedger, "Ledger", "账本")                                                             \
    X(MenuNetwork, "Network", "网络")                                                           \
    X(MenuSettings, "Settings", "设置")                                                         \
    X(MenuAbout, "About", "关于")                                                               \
    X(MenuReload, "Reload config", "重载配置")                                                  \
    X(MenuReboot, "Reboot", "重启设备")                                                         \
    X(MenuAcctFmt, "acct %s", "账户 %s")                                                        \
    X(HintMenu, "; / .  move    ENTER ok    ` back", "; / . 移动   ENTER 确定   ` 返回")        \
    /* —— 列表 —— */                                                                            \
    X(RowsFmt, "%u rows", "%u 条")                                                              \
    X(HintScroll, "; / .  scroll    ` back", "; / . 滚动   ` 返回")                             \
    X(LedgerTitle, "LEDGER", "账本")                                                            \
    X(LedgerFootFmt, "observed spend | %u days | %u book(s)",                                   \
      "已观测消费 | %u 天 | %u 本账")                                                           \
    X(LedgerEmpty, "(no records yet)", "(暂无记录)")                                            \
    /* —— 网络页 —— */                                                                          \
    X(NetTitle, "NETWORK", "网络")                                                              \
    X(NetState, "state", "状态")                                                                \
    X(NetSsid, "ssid", "热点")                                                                  \
    X(NetIp, "ip", "IP")                                                                        \
    X(NetRssi, "rssi", "信号")                                                                  \
    X(NetTimeSync, "time sync", "对时")                                                         \
    X(NetBeijing, "beijing", "北京时间")                                                        \
    X(NetApiKey, "api key", "密钥")                                                             \
    X(NetLastFetch, "last fetch", "上次请求")                                                   \
    X(NetLatency, "latency", "耗时")                                                            \
    X(NetCurrency, "currency", "币种")                                                          \
    X(NetAvailable, "available", "余额可用")                                                    \
    X(NetGranted, "granted", "赠金")                                                            \
    X(NetToppedUp, "topped up", "充值")                                                         \
    X(NetTls, "tls verify", "证书校验")                                                         \
    X(NetSd, "sd card", "SD 卡")                                                                \
    X(NetAccount, "account", "账户")                                                            \
    X(NetBooks, "books", "账本数")                                                              \
    X(NetMessage, "message", "消息")                                                            \
    X(ValOk, "ok", "正常")                                                                      \
    X(ValPending, "pending", "未完成")                                                          \
    X(ValSet, "set", "已配置")                                                                  \
    X(ValMissing, "missing", "未配置")                                                          \
    X(ValYes, "yes", "是")                                                                      \
    X(ValNo, "no", "否")                                                                        \
    X(ValNotPresent, "not present", "未插入")                                                   \
    X(ValLangName, "English", "中文")                                                            \
    /* —— 设置 —— */                                                                            \
    X(SettingsTitle, "SETTINGS", "设置")                                                        \
    X(SettingsSub, "saved to SD/NVS", "保存到 SD/NVS")                                          \
    X(SetBrightness, "Brightness", "亮度")                                                      \
    X(SetSound, "Sound", "音效")                                                                \
    X(SetVolume, "Volume", "音量")                                                              \
    X(SetRefresh, "Refresh", "刷新间隔")                                                        \
    X(SetTls, "Verify TLS", "证书校验")                                                         \
    X(SetBubbleClose, "Bubble close", "气泡自动关闭")                                           \
    X(SetSeconds, "Clock seconds", "时钟显示秒")                                                \
    X(SetLanguage, "Language", "语言")                                                          \
    X(SetSave, "Save now", "立即保存")                                                          \
    X(ValOn, "on", "开")                                                                        \
    X(ValOff, "off", "关")                                                                      \
    X(HintSettings, "< > change   ; / .  move   ` back", "< > 调整   ; / . 移动   ` 返回")      \
    /* —— 关于 —— */                                                                            \
    X(AboutTitle, "ABOUT", "关于")                                                              \
    X(AboutName, "DeepSeek Whale", "DeepSeek 小鲸鱼")                                           \
    X(AboutBoard, "Cardputer ADV", "Cardputer ADV")                                             \
    X(AboutVersionFmt, "version   %s", "版本     %s")                                           \
    X(AboutBuildFmt, "built     %s", "编译     %s")                                             \
    X(AboutSdFmt, "SD card   %s", "SD 卡    %s")                                                \
    X(AboutIpFmt, "IP        %s", "IP       %s")                                                \
    X(AboutAcctFmt, "account   %s | %u day(s)", "账户     %s | %u 天")                          \
    X(AboutCreditA, "port of MeteorNOX/DeepSeek-", "移植自 MeteorNOX/")                         \
    X(AboutCreditB, "Balance-Whale-Widget", "DeepSeek-Balance-Whale-Widget")                    \
    X(HintAbout, "MIT / upstream MIT (assets as-is)    ` back",                                 \
      "MIT / 上游素材 as-is   ` 返回")                                                  \
    /* —— 气泡 —— */                                                                            \
    X(BubbleWhaleSays, "WHALE SAYS", "鲸鱼说")                                                  \
    X(BubbleBalance, "BALANCE", "余额")                                                         \
    X(BubbleNoBalance, "NO BALANCE YET", "还没有余额")                                          \
    X(BubbleToday, "TODAY USED", "今日已用")                                                    \
    X(BubblePriceTier, "PRICE TIER", "计价时段")                                                \
    X(BubbleTotalSuffix, " total", " 总额")                                                     \
    X(BubbleGrantedPrefix, "granted  ", "赠金     ")                                            \
    X(BubbleToppedPrefix, "topped   ", "充值     ")                                             \
    X(BubbleObservedSuffix, " observed", " 已观测")                                             \
    X(BubbleDecreasePrefix, "decrease ", "下降     ")                                           \
    X(BubbleIncreasePrefix, "top-up   ", "充值     ")                                           \
    X(BubblePartial, "(partial data today)", "（今日数据不完整）")                              \
    X(BubbleFullDay, "(full day observed)", "（全天已观测）")                                   \
    X(BubbleNoObservation, "no observation yet today", "今天还没有观测")                        \
    X(BubbleBaseline, "the first fetch is the baseline", "第一次取数只是统计起点")              \
    X(BubblePeakNow, "PEAK now (x2 price)", "现在是高峰（双倍价）")                             \
    X(BubbleValleyNow, "OFF-PEAK now (half price)", "现在是空闲（半价）")                       \
    X(BubbleSwitchFmt, "switches in %lldm %02llds", "%lld 分 %02lld 秒后切换")                  \
    X(BubbleFlashPriceFmt, "flash out %.2f / %.2f CNY", "flash 输出 %.2f / %.2f 元")            \
    X(BubblePerMillion, "per million tokens", "每百万 token")                                   \
    X(BubbleWaitApi, "waiting for the API", "等待接口返回")                                     \
    X(BubbleNoWifi, "no WiFi yet", "还没连上 WiFi")                                             \
    /* —— 提示 / 启动 —— */                                                                     \
    X(ToastConfigSaved, "config saved", "配置已保存")                                           \
    X(ToastSaveFailed, "save failed", "保存失败")                                               \
    X(ToastRebooting, "rebooting...", "正在重启…")                                              \
    X(ToastConfigReloaded, "config reloaded", "配置已重载")                                     \
    X(ToastNoConfig, "no config found", "没找到配置文件")                                       \
    X(BootBooting, "booting", "启动中")                                                         \
    X(BootBoard, "M5Cardputer ADV", "M5Cardputer ADV")                                          \
    X(BootSdCard, "SD card", "SD 卡")                                                           \
    X(BootMounting, "mounting /dswhale", "挂载 /dswhale")                                       \
    X(BootWifi, "WiFi", "WiFi")                                                                 \
    X(BootWifiConnecting, "WiFi connecting", "WiFi 连接中")                                     \
    X(BootNtp, "NTP time sync", "NTP 对时")                                                     \
    X(BootBeijing, "Beijing time (UTC+8)", "北京时间 (UTC+8)")                                  \
    X(BootBalance, "balance", "余额")                                                           \
    X(BootBalanceUrl, "GET /user/balance", "GET /user/balance")                                 \
    X(BootNoWifiConfig, "no WiFi config", "没有 WiFi 配置")                                     \
    X(BootPutConfig, "put config.json on the SD card", "请在 SD 卡上放 config.json")            \
    X(BootTagline, "balance companion for M5Cardputer ADV", "M5Cardputer ADV 上的余额小鲸鱼")   \
    X(BootTitleA, "DEEPSEEK", "DEEPSEEK")                                                       \
    X(BootTitleB, "WHALE", "小鲸鱼")                                                            \
    /* —— 错误 —— */                                                                            \
    X(ErrNoWifiConfig, "no WiFi config", "未配置 WiFi")                                         \
    X(ErrWifiOffline, "WiFi offline", "WiFi 未连接")                                            \
    X(ErrNoWifi, "no WiFi", "无网络")

enum class Str : uint16_t {
#define X(name, en, zh) name,
    DSW_STRINGS(X)
#undef X
    Count
};

// 当前语言
Lang current();
void set(Lang l);
bool isCJK();

// 取文案；越界会返回 "?"，不会返回 nullptr
const char* t(Str s);

// 语言与配置字符串互转（"en" / "zh"）
const char* code();
Lang fromCode(const char* code, Lang fallback = Lang::En);

// ===== 字体 =====
// 英文用 M5FX 自带字体（6x8 点阵 / 8x16 点阵 / FreeSans 矢量）；
// 中文用 efontCN 子集字库。
struct FontSet {
    const lgfx::IFont* small;  // 小标签、状态栏
    const lgfx::IFont* mono;   // 菜单 / 列表 / 次要数值
    const lgfx::IFont* value;  // 主数值（大号）
    const lgfx::IFont* title;  // 启动页标题
};

const FontSet& fonts();

// ===== 鲸鱼随机台词（权重 + 两种语言）=====
struct BubbleLine {
    uint8_t weight;
    const char* text;
};

// 返回当前语言下的台词数组与条数
const BubbleLine* bubbleLines(size_t& count);

}  // namespace lang
