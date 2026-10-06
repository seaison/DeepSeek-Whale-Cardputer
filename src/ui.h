// 界面层：一块 240x135 的 M5Canvas 离屏缓冲，整屏重绘后 pushSprite。
// 布局（横屏 rotation=1）：
//   ┌──────────────────────────────────────────┐
//   │ 状态条: WiFi / 时钟 / 峰谷徽标            │  y 0..14
//   ├────────────┬─────────────────────────────┤
//   │ 鲸鱼 96x96 │  BALANCE 大数字              │
//   │            │  TODAY USED                  │
//   │            │  PRICE TIER + 倒计时         │
//   ├────────────┴─────────────────────────────┤
//   │ 提示/错误行                               │  y 120..134
//   └──────────────────────────────────────────┘
#pragma once

#include <M5Cardputer.h>

#include <string>
#include <utility>
#include <vector>

#include "ledger.h"

enum class Screen : uint8_t { Boot, Main, Menu, Ledger, Net, Settings, About };

// 一屏要用的全部状态，由 .ino 填好交给 Ui 渲染。
struct ViewModel {
    // 网络
    bool wifiConfigured = false;
    bool wifiOnline = false;
    const char* netText = "idle";
    int32_t rssi = 0;
    std::string ip;

    // 时间
    bool timeSynced = false;
    int64_t nowUtc = 0;

    // 余额
    bool haveBalance = false;
    bool fetching = false;
    int64_t total = 0;
    int64_t granted = 0;
    int64_t toppedUp = 0;
    char currency[8] = "CNY";
    int64_t balanceAt = 0;
    uint32_t latencyMs = 0;
    std::string lastError;

    // 记账
    ledger::Summary today;
    int bookCount = 1;
    size_t dayCount = 0;
    std::string scope;
    std::string firstDay;

    // 峰谷
    bool peak = false;
    int64_t nextChangeAt = 0;

    // 界面开关
    bool showSeconds = true;
    int bubbleCountdown = 0;  // >0 时气泡右下角显示自动关闭倒计时

    // 系统
    bool sdReady = false;
    std::string sdStatus;
    std::string version;
    std::string buildDate;
};

class Ui {
public:
    bool begin();  // 建离屏缓冲；失败返回 false
    M5Canvas& canvas() { return canvas_; }

    void drawBoot(const char* stage, const char* detail);
    void drawMain(const ViewModel& vm);
    void drawMenu(const ViewModel& vm, const std::vector<std::string>& items, int sel,
                  const char* subtitle);
    void drawList(const ViewModel& vm, const char* title,
                  const std::vector<std::pair<std::string, std::string>>& rows, int sel,
                  const char* footer);
    void drawSettings(const ViewModel& vm, const std::vector<std::string>& labels,
                      const std::vector<std::string>& values, int sel);
    void drawAbout(const ViewModel& vm);
    void drawBubble(const ViewModel& vm, const char* title, const std::vector<std::string>& lines);
    // 只画 toast 面板并推送——调用前屏幕内容已经画好了（不重绘底图）
    void drawToastOverlay(const char* text);

private:
    void statusBar(const ViewModel& vm, const char* rightBadge, uint16_t badgeColor);
    void footer(const char* text, uint16_t color);
    void panel(int x, int y, int w, int h, uint16_t fill, uint16_t border);
    void whale(int x, int y);

    M5Canvas canvas_{&M5Cardputer.Display};
    bool ready_ = false;
};
