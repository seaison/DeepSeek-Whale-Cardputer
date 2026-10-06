// 界面层：一块 240x135 的 M5Canvas 离屏缓冲，整屏重绘后 pushSprite。
//
// 布局（横屏 rotation=1）：
//   ┌──────────────────────────────────────────┐
//   │ 状态条: WiFi / 时钟 / 峰谷徽标            │  y 0..14
//   ├────────────┬─────────────────────────────┤
//   │ 鲸鱼 96x96 │  余额        110.00          │  行式布局：左标签 + 右数值
//   │            │  今日已用     1.23           │
//   │            │  高峰计价    2h 13m          │
//   ├────────────┴─────────────────────────────┤
//   │ 提示 / 错误行                             │  y 120..134
//   └──────────────────────────────────────────┘
//
// 双语：所有文案走 lang::t()，字体走 lang::fonts()（中文是 efontCN，字形更宽更高），
// 所以**行高与可视行数都是按当前字体实测出来的**，不是写死的常量。
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "assets/whale_96.h"
#include "lang.h"
#include "money.h"
#include "pricing.h"

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
constexpr int kColX = 108;  // 右侧数据列起点
constexpr int kColRight = kScreenW - 4;

void fmtMoney(int64_t units, char* out, size_t n) {
    money::format(units, out, n, 2);
}

// 把秒数差格式化成 "1d 03h" / "2h 13m" / "13m 05s"（纯数字+字母，中英通用）
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

// 按像素宽度折行。中英混排都能用：UTF-8 按首字节取整字（不会把汉字切两半），
// ASCII 尽量在空格处断，CJK 可以任意位置断。
void wrapText(M5Canvas& c, const char* text, int maxWidth, std::vector<std::string>& out,
              size_t maxLines) {
    out.clear();
    if (!text || !*text || maxLines == 0) return;
    std::string line;
    const char* p = text;
    while (*p) {
        size_t len = 1;
        const unsigned char ch = (unsigned char)*p;
        if (ch >= 0xF0) {
            len = 4;
        } else if (ch >= 0xE0) {
            len = 3;
        } else if (ch >= 0xC0) {
            len = 2;
        }
        const std::string glyph(p, len);
        p += len;

        const std::string probe = line + glyph;
        if (c.textWidth(probe.c_str()) > maxWidth && !line.empty()) {
            // ASCII 单词回退到最后一个空格，避免把单词劈开
            if (glyph != " " && (unsigned char)glyph[0] < 0x80) {
                const size_t sp = line.find_last_of(' ');
                if (sp != std::string::npos && sp + 1 < line.size()) {
                    out.push_back(line.substr(0, sp));
                    line = line.substr(sp + 1) + glyph;
                    if (out.size() >= maxLines) return;
                    continue;
                }
            }
            out.push_back(line);
            line = glyph;
            if (out.size() >= maxLines) return;
        } else {
            line = probe;
        }
    }
    if (out.size() < maxLines && !line.empty()) out.push_back(line);
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
    if (fabsf(whaleAngle_) < 0.5f) {
        canvas_.pushImage(x, y, WHALE_96_W, WHALE_96_H, whale_96);
        return;
    }
    // 旋转时同时缩一点：96x96 转到 45° 时外接框是 136px，会戳出 100px 的画框；
    // 0.68 倍后最大 92px，正好留在框里。角度按度（LovyanGFX 的 pushImageRotateZoom 用度）。
    const float t = fabsf(sinf(whaleAngle_ * 0.0174532925f));
    const float zoom = 1.0f - 0.32f * t;
    canvas_.pushImageRotateZoom(x + WHALE_96_W / 2.0f, y + WHALE_96_H / 2.0f,
                                WHALE_96_W / 2.0f, WHALE_96_H / 2.0f, whaleAngle_, zoom, zoom,
                                WHALE_96_W, WHALE_96_H, whale_96);
}

void Ui::statusBar(const ViewModel& vm, const char* rightBadge, uint16_t badgeColor) {
    const lang::FontSet& F = lang::fonts();
    canvas_.fillRect(0, 0, kScreenW, kStatusH, kPanel);
    canvas_.drawFastHLine(0, kStatusH - 1, kScreenW, kPanelEdge);

    canvas_.setTextDatum(textdatum_t::middle_left);
    canvas_.setFont(F.small);

    // 左：WiFi 状态
    const uint16_t dot = vm.wifiOnline ? kValley : (vm.wifiConfigured ? kPeak : kWarn);
    canvas_.fillCircle(7, kStatusH / 2, 3, dot);
    canvas_.setTextColor(kMuted);
    char left[48];
    if (vm.wifiOnline) {
        snprintf(left, sizeof(left), lang::t(lang::Str::WifiSignalFmt), (int)vm.rssi);
    } else {
        snprintf(left, sizeof(left), "%s",
                 lang::t(vm.wifiConfigured ? lang::Str::WifiConnecting : lang::Str::WifiNoConfig));
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
        snprintf(clock, sizeof(clock), "%s", lang::t(lang::Str::ClockPending));
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
    const lang::FontSet& F = lang::fonts();
    canvas_.fillRect(0, kFooterY, kScreenW, 15, kPanel);
    canvas_.drawFastHLine(0, kFooterY, kScreenW, kPanelEdge);
    canvas_.setTextDatum(textdatum_t::middle_center);
    canvas_.setFont(F.small);
    canvas_.setTextColor(color);
    canvas_.drawString(text, kScreenW / 2, kFooterY + 8);
}

void Ui::drawBoot(const char* stage, const char* detail) {
    if (!ready_) return;
    const lang::FontSet& F = lang::fonts();
    canvas_.fillSprite(kBgDeep);
    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(F.title);
    canvas_.setTextColor(kAccent);
    canvas_.drawString(lang::t(lang::Str::BootTitleA), 12, 12);
    canvas_.setTextColor(kText);
    canvas_.drawString(lang::t(lang::Str::BootTitleB), 12, 12 + canvas_.fontHeight());

    canvas_.setFont(F.small);
    canvas_.setTextColor(kMuted);
    canvas_.drawString(lang::t(lang::Str::BootTagline), 12, 88);

    canvas_.drawFastHLine(12, 100, 216, kPanelEdge);
    canvas_.setTextColor(kAccentSoft);
    canvas_.drawString(stage ? stage : "", 12, 104);
    canvas_.setTextColor(kMuted);
    if (detail) canvas_.drawString(detail, 12, 104 + canvas_.fontHeight() + 2);
    canvas_.pushSprite(0, 0);
}

void Ui::drawMain(const ViewModel& vm) {
    composeMain(vm);
    if (ready_) canvas_.pushSprite(0, 0);
}

void Ui::composeMain(const ViewModel& vm) {
    if (!ready_) return;
    const lang::FontSet& F = lang::fonts();
    canvas_.fillSprite(kBgDeep);

    // 状态条右侧徽标
    const char* badge;
    uint16_t badgeColor;
    if (!vm.timeSynced) {
        badge = lang::t(lang::Str::BadgeSyncTime);
        badgeColor = kMuted;
    } else if (vm.peak) {
        badge = lang::t(lang::Str::BadgePeak);
        badgeColor = kPeak;
    } else {
        badge = lang::t(lang::Str::BadgeOffPeak);
        badgeColor = kValley;
    }
    statusBar(vm, badge, badgeColor);

    // 左侧鲸鱼 + 淡描边
    panel(3, 18, 100, 100, kBgDeep, kPanelEdge);
    whale(5, 20);

    // 右侧三行：左标签 + 右数值。
    // 行高按当前字体实测；「标签 + 数值」在列宽里放不下时，数值自动降一档字体
    // —— 宁可小一点，也不要在中文/大额时压字。
    canvas_.setFont(F.value);
    const int hValue = (int)canvas_.fontHeight();
    canvas_.setFont(F.mono);
    const int hMono = (int)canvas_.fontHeight();
    constexpr int kColW = kColRight - kColX;

    auto drawRow = [&](int y, int rowH, const char* label, uint16_t labelColor, const char* value,
                       bool wantBig, uint16_t valueColor) {
        canvas_.setFont(F.small);
        const int labelW = (int)canvas_.textWidth(label);
        bool big = wantBig;
        if (big) {
            canvas_.setFont(F.value);
            if (labelW + (int)canvas_.textWidth(value) + 8 > kColW) big = false;  // 放不下就降档
        }
        const lgfx::IFont* vf = big ? F.value : F.mono;

        canvas_.setFont(F.small);
        canvas_.setTextColor(labelColor);
        canvas_.setTextDatum(textdatum_t::middle_left);
        canvas_.drawString(label, kColX, y + rowH / 2);

        canvas_.setFont(vf);
        canvas_.setTextColor(valueColor);
        canvas_.setTextDatum(textdatum_t::middle_right);
        canvas_.drawString(value, kColRight, y + rowH / 2);
    };

    char value[40];
    int y = 18;

    // —— 第 1 行：余额 ——
    if (vm.fetching && !vm.haveBalance) {
        drawRow(y, hValue, lang::t(lang::Str::Balance), kMuted, "......", true, kMuted);
    } else if (vm.haveBalance) {
        fmtMoney(vm.total, value, sizeof(value));
        drawRow(y, hValue, lang::t(lang::Str::Balance), kMuted, value, true, kText);
    } else {
        drawRow(y, hValue, lang::t(lang::Str::Balance), kMuted, lang::t(lang::Str::NoData), true,
                kWarn);
    }
    y += hValue + 4;

    // —— 第 2 行：今日已用 ——
    if (vm.today.valid) {
        fmtMoney(vm.today.amount, value, sizeof(value));
        drawRow(y, hMono, lang::t(lang::Str::TodayUsed), kMuted, value, false,
                vm.today.partialDay ? kAccentSoft : kText);
    } else {
        drawRow(y, hMono, lang::t(lang::Str::TodayUsed), kMuted, lang::t(lang::Str::NoData), false,
                kMuted);
    }
    y += hMono + 4;

    // —— 第 3 行：峰谷档位 + 下一切换倒计时 ——
    // 还没对时的时候只显示「等待对时」，不画标签（否则两段文字会挤在一起）
    if (vm.timeSynced && vm.nextChangeAt > vm.nowUtc) {
        char cd[24];
        fmtCountdown(vm.nextChangeAt - vm.nowUtc, cd, sizeof(cd));
        drawRow(y, hMono, lang::t(vm.peak ? lang::Str::PeakPrice : lang::Str::OffPeakPrice),
                vm.peak ? kPeak : kValley, cd, false, kMuted);
    } else {
        canvas_.setFont(F.mono);
        canvas_.setTextColor(kMuted);
        canvas_.setTextDatum(textdatum_t::middle_right);
        canvas_.drawString(lang::t(lang::Str::WaitingTime), kColRight, y + hMono / 2);
    }

    // 底栏
    char foot[80];
    if (!vm.lastError.empty()) {
        snprintf(foot, sizeof(foot), "! %s", vm.lastError.c_str());
        footer(foot, kWarn);
    } else if (vm.today.valid && !vm.today.partialDay) {
        char ago[24];
        fmtAgo(vm.nowUtc - vm.today.lastAt, ago, sizeof(ago));
        snprintf(foot, sizeof(foot), lang::t(lang::Str::HintUpdatedFmt), ago);
        footer(foot, kMuted);
    } else if (vm.today.valid) {
        footer(lang::t(lang::Str::HintPartialToday), kMuted);
    } else {
        footer(lang::t(lang::Str::HintMain), kMuted);
    }
    if (toastOn_) drawToastPanel();
}

void Ui::drawMenu(const ViewModel& vm, const std::vector<std::string>& items, int sel,
                  const char* subtitle) {
    if (!ready_) return;
    const lang::FontSet& F = lang::fonts();
    canvas_.fillSprite(kBgDeep);
    statusBar(vm,
              lang::t(vm.timeSynced
                          ? (vm.peak ? lang::Str::BadgePeak : lang::Str::BadgeOffPeak)
                          : lang::Str::BadgeSyncTime),
              vm.timeSynced ? (vm.peak ? kPeak : kValley) : kMuted);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(F.small);
    canvas_.setTextColor(kAccent);
    canvas_.drawString(lang::t(lang::Str::MenuTitle), 6, kStatusH + 3);
    if (subtitle) {
        canvas_.setTextColor(kMuted);
        canvas_.drawString(subtitle, 6 + canvas_.textWidth(lang::t(lang::Str::MenuTitle)) + 8,
                           kStatusH + 3);
    }

    canvas_.setFont(F.mono);
    const int rowH = (int)canvas_.fontHeight() + 2;
    const int y0 = kStatusH + 6 + (int)canvas_.fontHeight();
    int rows = (kFooterY - 2 - y0) / rowH;
    if (rows < 3) rows = 3;
    if (rows > 8) rows = 8;

    int top = sel - rows / 2;
    if (top < 0) top = 0;
    if (top > (int)items.size() - rows) top = (int)items.size() - rows;
    if (top < 0) top = 0;

    for (int i = 0; i < rows && top + i < (int)items.size(); ++i) {
        const int idx = top + i;
        const int y = y0 + i * rowH;
        const bool active = idx == sel;
        if (active) panel(3, y, kScreenW - 6, rowH, kAccent, kAccentSoft);
        canvas_.setTextColor(active ? kText : kMuted);
        char row[48];
        snprintf(row, sizeof(row), "%s%s", active ? "> " : "  ", items[idx].c_str());
        canvas_.drawString(row, 7, y);
    }
    footer(lang::t(lang::Str::HintMenu), kMuted);
    if (toastOn_) drawToastPanel();
    canvas_.pushSprite(0, 0);
}

void Ui::drawList(const ViewModel& vm, const char* title,
                  const std::vector<std::pair<std::string, std::string>>& rows, int sel,
                  const char* foot) {
    if (!ready_) return;
    const lang::FontSet& F = lang::fonts();
    canvas_.fillSprite(kBgDeep);
    statusBar(vm, lang::t(vm.peak ? lang::Str::BadgePeak : lang::Str::BadgeOffPeak),
              vm.peak ? kPeak : kValley);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(F.small);
    canvas_.setTextColor(kAccent);
    canvas_.drawString(title, 6, kStatusH + 3);

    char cnt[24];
    snprintf(cnt, sizeof(cnt), lang::t(lang::Str::RowsFmt), (unsigned)rows.size());
    canvas_.setTextColor(kMuted);
    canvas_.setTextDatum(textdatum_t::top_right);
    canvas_.drawString(cnt, kScreenW - 6, kStatusH + 3);

    canvas_.setFont(F.mono);
    const int rowH = (int)canvas_.fontHeight() + 2;
    const int y0 = kStatusH + 6 + (int)canvas_.fontHeight();
    int visible = (kFooterY - 2 - y0) / rowH;
    if (visible < 3) visible = 3;
    if (visible > 8) visible = 8;

    int top = sel - visible / 2;
    if (top < 0) top = 0;
    if (top > (int)rows.size() - visible) top = (int)rows.size() - visible;
    if (top < 0) top = 0;

    for (int i = 0; i < visible && top + i < (int)rows.size(); ++i) {
        const int idx = top + i;
        const int y = y0 + i * rowH;
        if (idx == sel) panel(3, y - 1, kScreenW - 6, rowH, kPanel, kPanelEdge);
        canvas_.setTextColor(idx == sel ? kText : kMuted);
        canvas_.setTextDatum(textdatum_t::top_left);
        canvas_.drawString(rows[idx].first.c_str(), 7, y);
        canvas_.setTextColor(idx == sel ? kAccentSoft : kMuted);
        canvas_.setTextDatum(textdatum_t::top_right);
        canvas_.drawString(rows[idx].second.c_str(), kScreenW - 7, y);
    }
    footer(foot ? foot : lang::t(lang::Str::HintScroll), kMuted);
    if (toastOn_) drawToastPanel();
    canvas_.pushSprite(0, 0);
}

void Ui::drawSettings(const ViewModel& vm, const std::vector<std::string>& labels,
                      const std::vector<std::string>& values, int sel) {
    if (!ready_) return;
    const lang::FontSet& F = lang::fonts();
    canvas_.fillSprite(kBgDeep);
    statusBar(vm, lang::t(vm.peak ? lang::Str::BadgePeak : lang::Str::BadgeOffPeak),
              vm.peak ? kPeak : kValley);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(F.small);
    canvas_.setTextColor(kAccent);
    canvas_.drawString(lang::t(lang::Str::SettingsTitle), 6, kStatusH + 3);
    canvas_.setTextColor(kMuted);
    canvas_.drawString(lang::t(lang::Str::SettingsSub),
                       6 + canvas_.textWidth(lang::t(lang::Str::SettingsTitle)) + 8, kStatusH + 3);

    canvas_.setFont(F.mono);
    const int rowH = (int)canvas_.fontHeight() + 3;
    const int y0 = kStatusH + 6 + (int)canvas_.fontHeight();
    int visible = (kFooterY - 2 - y0) / rowH;
    if (visible < 3) visible = 3;
    if (visible > 8) visible = 8;

    int top = sel - visible / 2;
    if (top < 0) top = 0;
    if (top > (int)labels.size() - visible) top = (int)labels.size() - visible;
    if (top < 0) top = 0;

    for (int i = 0; i < visible && top + i < (int)labels.size(); ++i) {
        const int idx = top + i;
        const int y = y0 + i * rowH;
        if (idx == sel) panel(3, y - 1, kScreenW - 6, rowH, kPanel, kPanelEdge);
        canvas_.setTextColor(idx == sel ? kText : kMuted);
        canvas_.setTextDatum(textdatum_t::top_left);
        canvas_.drawString(labels[idx].c_str(), 7, y);
        canvas_.setTextColor(idx == sel ? kAccentSoft : kMuted);
        canvas_.setTextDatum(textdatum_t::top_right);
        char v[32];
        snprintf(v, sizeof(v), "< %s >", values[idx].c_str());
        canvas_.drawString(v, kScreenW - 7, y);
    }
    footer(lang::t(lang::Str::HintSettings), kMuted);
    if (toastOn_) drawToastPanel();
    canvas_.pushSprite(0, 0);
}

void Ui::drawAbout(const ViewModel& vm) {
    if (!ready_) return;
    const lang::FontSet& F = lang::fonts();
    canvas_.fillSprite(kBgDeep);
    statusBar(vm, lang::t(vm.peak ? lang::Str::BadgePeak : lang::Str::BadgeOffPeak),
              vm.peak ? kPeak : kValley);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(F.small);
    canvas_.setTextColor(kAccent);
    canvas_.drawString(lang::t(lang::Str::AboutTitle), 6, kStatusH + 3);

    canvas_.setFont(F.mono);
    const int rowH = (int)canvas_.fontHeight() + 2;
    int y = kStatusH + 6 + (int)canvas_.fontHeight();
    canvas_.setTextColor(kText);
    canvas_.drawString(lang::t(lang::Str::AboutName), 6, y);
    canvas_.setTextColor(kAccentSoft);
    canvas_.setTextDatum(textdatum_t::top_right);
    canvas_.drawString(lang::t(lang::Str::AboutBoard), kScreenW - 6, y);
    canvas_.setTextDatum(textdatum_t::top_left);
    y += rowH + 2;

    canvas_.setFont(F.small);
    const int lh = (int)canvas_.fontHeight() + 3;
    canvas_.setTextColor(kMuted);
    char line[80];

    snprintf(line, sizeof(line), lang::t(lang::Str::AboutVersionFmt), vm.version.c_str());
    canvas_.drawString(line, 6, y);
    y += lh;
    snprintf(line, sizeof(line), lang::t(lang::Str::AboutBuildFmt), vm.buildDate.c_str());
    canvas_.drawString(line, 6, y);
    y += lh;
    snprintf(line, sizeof(line), lang::t(lang::Str::AboutSdFmt),
             vm.sdReady ? vm.sdStatus.c_str() : lang::t(lang::Str::ValNotPresent));
    canvas_.drawString(line, 6, y);
    y += lh;
    snprintf(line, sizeof(line), lang::t(lang::Str::AboutIpFmt), vm.ip.c_str());
    canvas_.drawString(line, 6, y);
    y += lh;
    snprintf(line, sizeof(line), lang::t(lang::Str::AboutAcctFmt), vm.scope.c_str(),
             (unsigned)vm.dayCount);
    canvas_.drawString(line, 6, y);
    y += lh;

    if (y < kFooterY - lh) {
        canvas_.drawString(lang::t(lang::Str::AboutCreditA), 6, y);
        y += lh;
        canvas_.drawString(lang::t(lang::Str::AboutCreditB), 6, y);
    }

    footer(lang::t(lang::Str::HintAbout), kMuted);
    if (toastOn_) drawToastPanel();
    canvas_.pushSprite(0, 0);
}

void Ui::drawBubble(const ViewModel& vm, const char* title, const std::vector<std::string>& lines) {
    if (!ready_) return;
    const lang::FontSet& F = lang::fonts();
    composeMain(vm);  // 先把主屏画进缓冲（不推屏），气泡盖上去后只推一次

    const int x = 84;
    const int w = kScreenW - x - 4;
    const int padX = 6;

    canvas_.setFont(F.small);
    const int lineH = (int)canvas_.fontHeight() + 3;

    // 文案超宽就折行（中文一句话十几个字就超了），最多 5 行
    std::vector<std::string> all;
    std::vector<std::string> wrapped;
    for (const auto& l : lines) {
        if (all.size() >= 5) break;
        wrapText(canvas_, l.c_str(), w - padX * 2, wrapped, 5 - all.size());
        for (const auto& s : wrapped) all.push_back(s);
    }

    int h = 18 + (int)all.size() * lineH + 4;
    if (h > 100) h = 100;
    const int y = 16;

    // 指向鲸鱼的小尾巴
    canvas_.fillTriangle(x - 7, y + 26, x + 1, y + 20, x + 1, y + 34, kAccent);
    panel(x, y, w, h, kPanel, kAccent);
    canvas_.drawFastHLine(x + 1, y + 15, w - 2, kPanelEdge);

    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setFont(F.small);
    canvas_.setTextColor(kAccentSoft);
    canvas_.drawString(title ? title : "", x + padX, y + 3);

    canvas_.setTextColor(kText);
    int ty = y + 17;
    for (const auto& l : all) {
        if (ty > y + h - lineH) break;
        canvas_.drawString(l.c_str(), x + padX, ty);
        ty += lineH;
    }

    // 右下角提示自动关闭
    if (vm.bubbleCountdown > 0) {
        char hint[16];
        snprintf(hint, sizeof(hint), "%ds", vm.bubbleCountdown);
        canvas_.setTextDatum(textdatum_t::bottom_right);
        canvas_.setTextColor(kMuted);
        canvas_.drawString(hint, x + w - padX, y + h - 3);
        canvas_.setTextDatum(textdatum_t::top_left);
    }
    if (toastOn_) drawToastPanel();
    canvas_.pushSprite(0, 0);
}

void Ui::setToast(const char* text) {
    if (!text || !*text) {
        toastOn_ = false;
        toastText_[0] = '\0';
        return;
    }
    snprintf(toastText_, sizeof(toastText_), "%s", text);
    toastOn_ = true;
}

void Ui::drawToastPanel() {
    if (!ready_ || !toastOn_) return;
    const lang::FontSet& F = lang::fonts();
    canvas_.setFont(F.small);
    const int lineH = (int)canvas_.fontHeight() + 3;
    std::vector<std::string> lines;
    wrapText(canvas_, toastText_, kScreenW - 24, lines, 2);

    int tw = 0;
    for (const auto& l : lines) {
        const int w = (int)canvas_.textWidth(l.c_str());
        if (w > tw) tw = w;
    }
    const int boxW = (tw + 20 < kScreenW - 8) ? tw + 20 : kScreenW - 8;
    const int boxH = (int)lines.size() * lineH + 8;
    const int x = (kScreenW - boxW) / 2;
    const int y = kFooterY - boxH - 2;

    panel(x, y, boxW, boxH, kPanel, kAccent);
    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.setTextColor(kText);
    int ty = y + 4;
    for (const auto& l : lines) {
        canvas_.drawString(l.c_str(), x + 10, ty);
        ty += lineH;
    }
}
