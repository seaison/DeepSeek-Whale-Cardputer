#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "money.h"
#include "pricing.h"
#include "assets/whale_96.h"

namespace {

// ===== 主题色 =====
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t kBgDeep = rgb(7, 11, 22);
constexpr uint16_t kPanel = rgb(17, 25, 44);
constexpr uint16_t kPanelEdge = rgb(38, 54, 92);
constexpr uint16_t kAccent = rgb(77, 107, 254);  // DeepSeek 蓝
constexpr uint16_t kAccentSoft = rgb(120, 150, 255);
constexpr uint16_t kText = rgb(233, 238, 250);
constexpr uint16_t kMuted = rgb(128, 143, 172);
constexpr uint16_t kPeak = rgb(255, 146, 74);
constexpr uint16_t kValley = rgb(64, 214, 162);
constexpr uint16_t kWarn = rgb(255, 92, 92);

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kStatusH = 15;
constexpr int kFooterY = kScreenH - 15;

const lgfx::IFont* kSmall = &fonts::Font0;              // 6x8
const lgfx::IFont* kMono = &fonts::AsciiFont8x16;       // 8x16
const lgfx::IFont* kValue = &fonts::FreeSans9pt7b;      // 数值
const lgfx::IFont* kTitle = &fonts::Orbitron_Light_24;  // 标题/大数字

void fmtMoney(int64_t units, char* out, size_t n) {
    money::format(units, out, n, 2);
}

// 把秒数差格式化成 "1d 03h" / "2h 13m" / "13m 05s"
void fmtCountdown(int64_t sec, char* out, size_t n) {
    if (sec < 0) sec = 0;
    const int64_t d = sec / 86400;
    const int64_t h = (sec % 86400) / 3600;
    const int64_t m = (sec % 3600) / 60;
    const int64_t s = sec % 60;
    if (d > 0) {
        snprintf(out, n, "%lldd %02lldh", (long long)d, (long long)h);
    } else if (h > 0) {
        snprintf(out, n, "%lldh %02lldm", (long long)h, (long long)m);
    } else {
        snprintf(out, n, "%lldm %02llds", (long long)m, (long long)s);
    }
}

void fmtAgo(int64_t sec, char* out, size_t n) {
    if (sec < 0) sec = 0;
    if (sec < 60) {
        snprintf(out, n, "%llds", (long long)sec);
    } else if (sec < 3600) {
        snprintf(out, n, "%lldm", (long long)sec / 60);
    } else {
        snprintf(out, n, "%lldh", (long long)sec / 3600);
    }
}

}  // namespace

bool Ui::begin() {
    canvas_.setColorDepth(16);
    canvas_.setPsram(true);  // ADV 有 8MB PSRAM，离屏缓冲放那儿最省内部 RAM
    if (!canvas_.createSprite(kScreenW, kScreenH)) {
        canvas_.setPsram(false);  // 没开 PSRAM 就退回内部 RAM（约 65KB）
        if (!canvas_.createSprite(kScreenW, kScreenH)) {
            ready_ = false;
            return false;
        }
    }
    ready_ = true;
    canvas_.setTextWrap(false);
    return ready_;
}

void Ui::panel(int x, int y, int w, int h, uint16_t fill, uint16_t border) {
    canvas_.fillRoundRect(x, y, w, h, 4, fill);
    canvas_.drawRoundRect(x, y, w, h, 4, border);
}

void Ui::whale(int x, int y) {
    canvas_.pushImage(x, y, WHALE_96_W, WHALE_96_H, whale_96);
}

void Ui::statusBar(const ViewModel& vm, const char* rightBadge, uint16_t badgeColor) {
    canvas_.fillRect(0, 0, kScreenW, kStatusH, kPanel);
    canvas_.drawFastHLine(0, kStatusH - 1, kScreenW, kPanelEdge);

    canvas_.setTextDatum(textdatum_t::middle_left);
    canvas_.setFont(kSmall);

    // 左：WiFi 状态
    uint16_t dot = vm.wifiOnline ? kValley : (vm.wifiConfigured ? kPeak : kWarn);
    canvas_.fillCircle(7, kStatusH / 2, 3, dot);
    canvas_.setTextColor(kMuted);
    char left[40];
    if (vm.wifiOnline) {
        snprintf(left, sizeof(left), "WiFi %ddBm", (int)vm.rssi);
    } else {
        snprintf(left, sizeof(left), "%s", vm.wifiConfigured ? "WiFi..." : "No WiFi");
    }
    canvas_.drawString(left, 14, kStatusH / 2);

    // 中：时间（北京时间）
    canvas_.setTextDatum(textdatum_t::middle_center);
    canvas_.setTextColor(vm.timeSynced ? kText : kMuted);
    char clock[16];
    if (vm.timeSynced) {
        const pricing::BeijingTime bt = pricing::beijing(vm.nowUtc);
        if (vm.showSeconds) {
            snprintf(clock, sizeof(clock), "%02d:%02d:%02d", bt.hour, bt.minute, bt.second);
        } else {
            snprintf(clock, sizeof(clock), "%02d:%02d", bt.hour, bt.minute);
        }
    } else {
        snprintf(clock, sizeof(clock), "--:--");
    }
    canvas_.drawString(clock, kScreenW / 2, kStatusH / 2);

    // 右：峰谷徽标
    if (rightBadge) {
        canvas_.setTextDatum(textdatum_t::middle_right);
        canvas_.setTextColor(badgeColor);
        canvas_.drawString(rightBadge, kScreenW - 4, kStatusH / 2);
    }
}

void Ui::footer(const char* text, uint16_t color) {
    canvas_.fillRect(0, kFooterY, kScreenW, 15, kPanel);
    canvas_.drawFastHLine(0, kFooterY, kScreenW, kPanelEdge);
    canvas_.setTextDatum(textdatum_t::middle_center);
    canvas_.setFont(kSmall);
    canvas_.setTextColor(color);
    canvas_.drawString(text, kScreenW / 2, kFooterY + 8);
}

void Ui::drawBoot(const char* stage, const char* detail) {
    if (!ready_) return;
    canvas_.fillSprite(kBgDeep);
    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(kTitle);
    canvas_.setTextColor(kAccent);
    canvas_.drawString("DEEPSEEK", 12, 14);
    canvas_.setTextColor(kText);
    canvas_.drawString("WHALE", 12, 40);

    canvas_.setFont(kSmall);
    canvas_.setTextColor(kMuted);
    canvas_.drawString("balance companion for M5Cardputer ADV", 12, 74);

    canvas_.drawFastHLine(12, 90, 216, kPanelEdge);
    canvas_.setTextColor(kAccentSoft);
    canvas_.drawString(stage ? stage : "", 12, 96);
    canvas_.setTextColor(kMuted);
    if (detail) canvas_.drawString(detail, 12, 108);
    canvas_.pushSprite(0, 0);
}

void Ui::drawMain(const ViewModel& vm) {
    if (!ready_) return;
    canvas_.fillSprite(kBgDeep);

    // 状态条右侧徽标
    char badge[16];
    uint16_t badgeColor;
    if (!vm.timeSynced) {
        snprintf(badge, sizeof(badge), "SYNC TIME");
        badgeColor = kMuted;
    } else if (vm.peak) {
        snprintf(badge, sizeof(badge), "PEAK");
        badgeColor = kPeak;
    } else {
        snprintf(badge, sizeof(badge), "OFF-PEAK");
        badgeColor = kValley;
    }
    statusBar(vm, badge, badgeColor);

    // 左侧鲸鱼：加一层淡淡的描边，避免图形直接压在深色底上发飘
    panel(3, 18, 100, 100, kBgDeep, kPanelEdge);
    whale(5, 20);

    // 右侧数据列
    int y = 20;
    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(kSmall);
    canvas_.setTextColor(kMuted);
    char line[48];
    snprintf(line, sizeof(line), "BALANCE  %s", vm.currency);
    canvas_.drawString(line, 108, y);
    y += canvas_.fontHeight() + 3;

    canvas_.setFont(kValue);
    if (vm.fetching && !vm.haveBalance) {
        canvas_.setTextColor(kMuted);
        canvas_.drawString("......", 108, y);
    } else if (vm.haveBalance) {
        canvas_.setTextColor(kText);
        fmtMoney(vm.total, line, sizeof(line));
        canvas_.drawString(line, 108, y);
    } else {
        canvas_.setTextColor(kWarn);
        canvas_.drawString("--", 108, y);
    }
    y += canvas_.fontHeight() + 4;

    canvas_.setFont(kSmall);
    canvas_.setTextColor(kMuted);
    canvas_.drawString("TODAY USED", 108, y);
    y += canvas_.fontHeight() + 3;

    canvas_.setFont(kValue);
    if (vm.today.valid) {
        canvas_.setTextColor(vm.today.partialDay ? kAccentSoft : kText);
        fmtMoney(vm.today.amount, line, sizeof(line));
        canvas_.drawString(line, 108, y);
    } else {
        canvas_.setTextColor(kMuted);
        canvas_.drawString("--", 108, y);
    }
    y += canvas_.fontHeight() + 4;

    // 峰谷 + 倒计时
    canvas_.setFont(kSmall);
    canvas_.setTextColor(vm.peak ? kPeak : kValley);
    const char* tierName = vm.peak ? "PEAK PRICE" : "OFF-PEAK PRICE";
    canvas_.drawString(tierName, 108, y);
    y += canvas_.fontHeight() + 3;
    canvas_.setTextColor(kMuted);
    if (vm.nextChangeAt > vm.nowUtc) {
        char cd[24];
        fmtCountdown(vm.nextChangeAt - vm.nowUtc, cd, sizeof(cd));
        snprintf(line, sizeof(line), "switch in %s", cd);
    } else {
        snprintf(line, sizeof(line), "waiting for time");
    }
    canvas_.drawString(line, 108, y);

    // 底栏：优先显示错误，其次显示上次更新时间
    char foot[64];
    if (!vm.lastError.empty()) {
        snprintf(foot, sizeof(foot), "! %s", vm.lastError.c_str());
        footer(foot, kWarn);
    } else if (vm.today.valid && !vm.today.partialDay) {
        char ago[24];
        fmtAgo(vm.nowUtc - vm.today.lastAt, ago, sizeof(ago));
        snprintf(foot, sizeof(foot), "TAB menu   ENTER bubble   updated %s ago", ago);
        footer(foot, kMuted);
    } else {
        footer("TAB menu   ENTER bubble   R refresh", kMuted);
    }
    canvas_.pushSprite(0, 0);
}

void Ui::drawMenu(const ViewModel& vm, const std::vector<std::string>& items, int sel,
                  const char* subtitle) {
    if (!ready_) return;
    canvas_.fillSprite(kBgDeep);
    statusBar(vm, vm.timeSynced ? (vm.peak ? "PEAK" : "OFF-PEAK") : "SYNC TIME",
              vm.timeSynced ? (vm.peak ? kPeak : kValley) : kMuted);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(kSmall);
    canvas_.setTextColor(kAccent);
    canvas_.drawString("MENU", 6, kStatusH + 3);
    if (subtitle) {
        canvas_.setTextColor(kMuted);
        canvas_.drawString(subtitle, 44, kStatusH + 3);
    }

    // 6 行可视窗口，跟随选择滚动
    const int rows = 6;
    int top = sel - rows / 2;
    if (top < 0) top = 0;
    if (top > (int)items.size() - rows) top = (int)items.size() - rows;
    if (top < 0) top = 0;

    const int y0 = kStatusH + 14;
    for (int i = 0; i < rows && top + i < (int)items.size(); ++i) {
        const int idx = top + i;
        const int y = y0 + i * 15;
        const bool active = idx == sel;
        if (active) {
            panel(3, y - 2, kScreenW - 6, 15, kAccent, kAccentSoft);
        }
        canvas_.setFont(kMono);
        canvas_.setTextColor(active ? kText : kMuted);
        char row[40];
        snprintf(row, sizeof(row), "%s%s", active ? "> " : "  ", items[idx].c_str());
        canvas_.drawString(row, 7, y);
    }
    footer("; / .  move   ENTER ok   ` back", kMuted);
    canvas_.pushSprite(0, 0);
}

void Ui::drawList(const ViewModel& vm, const char* title,
                  const std::vector<std::pair<std::string, std::string>>& rows, int sel,
                  const char* foot) {
    if (!ready_) return;
    canvas_.fillSprite(kBgDeep);
    statusBar(vm, vm.peak ? "PEAK" : "OFF-PEAK", vm.peak ? kPeak : kValley);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(kSmall);
    canvas_.setTextColor(kAccent);
    canvas_.drawString(title, 6, kStatusH + 3);
    canvas_.setTextColor(kMuted);
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%u rows", (unsigned)rows.size());
    canvas_.drawString(cnt, 150, kStatusH + 3);

    const int visible = 6;
    int top = sel - visible / 2;
    if (top < 0) top = 0;
    if (top > (int)rows.size() - visible) top = (int)rows.size() - visible;
    if (top < 0) top = 0;

    const int y0 = kStatusH + 14;
    canvas_.setFont(kMono);
    for (int i = 0; i < visible && top + i < (int)rows.size(); ++i) {
        const int idx = top + i;
        const int y = y0 + i * 15;
        if (idx == sel) panel(3, y - 2, kScreenW - 6, 15, kPanel, kPanelEdge);
        canvas_.setTextColor(idx == sel ? kText : kMuted);
        canvas_.drawString(rows[idx].first.c_str(), 7, y);
        canvas_.setTextColor(idx == sel ? kAccentSoft : kMuted);
        canvas_.setTextDatum(textdatum_t::top_right);
        canvas_.drawString(rows[idx].second.c_str(), kScreenW - 7, y);
        canvas_.setTextDatum(textdatum_t::top_left);
    }
    footer(foot ? foot : "; / .  scroll   ` back", kMuted);
    canvas_.pushSprite(0, 0);
}

void Ui::drawSettings(const ViewModel& vm, const std::vector<std::string>& labels,
                      const std::vector<std::string>& values, int sel) {
    if (!ready_) return;
    canvas_.fillSprite(kBgDeep);
    statusBar(vm, vm.peak ? "PEAK" : "OFF-PEAK", vm.peak ? kPeak : kValley);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(kSmall);
    canvas_.setTextColor(kAccent);
    canvas_.drawString("SETTINGS", 6, kStatusH + 3);
    canvas_.setTextColor(kMuted);
    canvas_.drawString("stored to SD/NVS", 60, kStatusH + 3);

    const int visible = 5;
    int top = sel - visible / 2;
    if (top < 0) top = 0;
    if (top > (int)labels.size() - visible) top = (int)labels.size() - visible;
    if (top < 0) top = 0;

    const int y0 = kStatusH + 16;
    canvas_.setFont(kMono);
    for (int i = 0; i < visible && top + i < (int)labels.size(); ++i) {
        const int idx = top + i;
        const int y = y0 + i * 16;
        if (idx == sel) panel(3, y - 2, kScreenW - 6, 16, kPanel, kPanelEdge);
        canvas_.setTextColor(idx == sel ? kText : kMuted);
        canvas_.drawString(labels[idx].c_str(), 7, y);
        canvas_.setTextColor(idx == sel ? kAccentSoft : kMuted);
        canvas_.setTextDatum(textdatum_t::top_right);
        char v[32];
        snprintf(v, sizeof(v), "< %s >", values[idx].c_str());
        canvas_.drawString(v, kScreenW - 7, y);
        canvas_.setTextDatum(textdatum_t::top_left);
    }
    footer("< > change   ; / .  move   ` back", kMuted);
    canvas_.pushSprite(0, 0);
}

void Ui::drawAbout(const ViewModel& vm) {
    if (!ready_) return;
    canvas_.fillSprite(kBgDeep);
    statusBar(vm, vm.peak ? "PEAK" : "OFF-PEAK", vm.peak ? kPeak : kValley);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(kSmall);
    canvas_.setTextColor(kAccent);
    canvas_.drawString("ABOUT", 6, kStatusH + 3);

    int y = kStatusH + 18;
    canvas_.setFont(kMono);
    char line[64];
    canvas_.setTextColor(kText);
    canvas_.drawString("DeepSeek Whale", 6, y);
    canvas_.setTextColor(kAccentSoft);
    canvas_.drawString("Cardputer ADV", 120, y);
    y += 15;
    canvas_.setFont(kSmall);
    canvas_.setTextColor(kMuted);

    snprintf(line, sizeof(line), "version   %s", vm.version.c_str());
    canvas_.drawString(line, 6, y); y += 11;
    snprintf(line, sizeof(line), "built     %s", vm.buildDate.c_str());
    canvas_.drawString(line, 6, y); y += 11;
    snprintf(line, sizeof(line), "SD card   %s", vm.sdReady ? vm.sdStatus.c_str() : "not present");
    canvas_.drawString(line, 6, y); y += 11;
    snprintf(line, sizeof(line), "IP        %s", vm.ip.c_str());
    canvas_.drawString(line, 6, y); y += 11;
    snprintf(line, sizeof(line), "account   %s · %u day(s)", vm.scope.c_str(), (unsigned)vm.dayCount);
    canvas_.drawString(line, 6, y); y += 11;
    canvas_.drawString("port of MeteorNOX/DeepSeek-", 6, y);
    y += 11;
    canvas_.drawString("Balance-Whale-Widget", 6, y);

    footer("MIT · upstream MIT (assets as-is)   ` back", kMuted);
    canvas_.pushSprite(0, 0);
}

void Ui::drawBubble(const ViewModel& vm, const char* title, const std::vector<std::string>& lines) {
    if (!ready_) return;
    drawMain(vm);  // 先画主屏，气泡盖在上面

    const int x = 96;
    const int w = kScreenW - x - 4;
    int h = 20 + (int)lines.size() * 11 + 8;
    if (h > 96) h = 96;
    const int y = 20;

    // 指向鲸鱼的小尾巴
    canvas_.fillTriangle(x - 7, y + 26, x + 1, y + 20, x + 1, y + 34, kAccent);
    panel(x, y, w, h, kPanel, kAccent);
    canvas_.drawFastHLine(x + 1, y + 15, w - 2, kPanelEdge);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(kSmall);
    canvas_.setTextColor(kAccentSoft);
    canvas_.drawString(title ? title : "", x + 6, y + 4);

    canvas_.setTextColor(kText);
    int ty = y + 18;
    for (const auto& l : lines) {
        if (ty > y + h - 9) break;
        canvas_.drawString(l.c_str(), x + 6, ty);
        ty += 11;
    }

    // 右下角提示自动关闭
    if (vm.bubbleCountdown > 0) {
        char hint[16];
        snprintf(hint, sizeof(hint), "%ds", vm.bubbleCountdown);
        canvas_.setTextDatum(textdatum_t::bottom_right);
        canvas_.setTextColor(kMuted);
        canvas_.drawString(hint, x + w - 6, y + h - 3);
        canvas_.setTextDatum(textdatum_t::top_left);
    }
    canvas_.pushSprite(0, 0);
}

void Ui::drawToastOverlay(const char* text) {
    if (!ready_ || !text) return;
    canvas_.setFont(kSmall);
    const int tw = canvas_.textWidth(text) + 16;
    const int x = (kScreenW - tw) / 2;
    const int y = kFooterY - 22;
    panel(x, y, tw, 18, kPanel, kAccent);
    canvas_.setTextDatum(textdatum_t::middle_center);
    canvas_.setTextColor(kText);
    canvas_.drawString(text, kScreenW / 2, y + 9);
    canvas_.pushSprite(0, 0);
}
