// 界面层：一块 240x135 的 M5Canvas 离屏缓冲，整屏重绘后 pushSprite。
//
// 布局（横屏 rotation=1）：
//   ┌──────────────────────────────────────────┐
//   │ 状态条: WiFi / 时钟 / 电量 / 峰谷徽标      │  y 0..14
//   ├────────────┬─────────────────────────────┤
//   │ 鲸鱼 96x96 │  余额                        │  右列：余额是「左上标签 +
//   │            │                    110.00    │  压在块最下面一行的右对齐大号数值」
//   │            │  今日已用            1.23     │  的块；下面两行是「左标签 + 右数值」；
//   │            │  高峰计价          2h 13m     │  剩余竖直空间均分成两个间隙
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
    // 位图背景是纯黑，按「黑=透明」贴：画框底色比纯黑略亮，不透明时动画中会看到
    // 一块跟着动的黑方块。（检查过：图里没有被内容完全包围的纯黑像素，不会打洞。）
    constexpr uint16_t kTransparent = 0x0000;
    const bool atRest = (fabsf(whaleSx_ - 1.0f) < 0.004f && fabsf(whaleSy_ - 1.0f) < 0.004f);
    if (atRest) {
        canvas_.pushImage(x, y, WHALE_96_W, WHALE_96_H, whale_96, kTransparent);
        return;
    }
    // 回弹：以**底部中点**为枢轴做非等比缩放（src 枢轴 (48,96) 映射到 dst 枢轴
    // (x+48, y+96)），这样压扁时底部坐标不变。角度固定 0，不旋转。
    canvas_.pushImageRotateZoom(x + WHALE_96_W / 2.0f, y + (float)WHALE_96_H,
                                WHALE_96_W / 2.0f, (float)WHALE_96_H, 0.0f, whaleSx_, whaleSy_,
                                WHALE_96_W, WHALE_96_H, whale_96, kTransparent);
}

void Ui::statusBar(const ViewModel& vm, const char* rightBadge, uint16_t badgeColor) {
    const lang::FontSet& F = lang::fonts();
    canvas_.fillRect(0, 0, kScreenW, kStatusH, kPanel);
    canvas_.drawFastHLine(0, kStatusH - 1, kScreenW, kPanelEdge);
    canvas_.setFont(F.small);

    // 左：WiFi 状态
    canvas_.setTextDatum(textdatum_t::middle_left);
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

    // 右：峰谷徽标（先量宽度——电量要排在它左边）
    const int badgeW = rightBadge ? (int)canvas_.textWidth(rightBadge) : 0;
    if (rightBadge) {
        canvas_.setTextDatum(textdatum_t::middle_right);
        canvas_.setTextColor(badgeColor);
        canvas_.drawString(rightBadge, kScreenW - 4, kStatusH / 2);
    }

    // 中：时间（北京时间）
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
    canvas_.setTextDatum(textdatum_t::middle_center);
    canvas_.setTextColor(vm.timeSynced ? kText : kMuted);
    canvas_.drawString(clock, kScreenW / 2, kStatusH / 2);

    // 电量：夹在时钟与徽标之间。位置全部按实测宽度排，挤不下就先丢图标、再丢文字。
    if (vm.batteryPercent >= 0) {
        int pctVal = vm.batteryPercent;  // 显式夹紧，避免 %d 的溢出告警
        if (pctVal > 100) pctVal = 100;
        if (pctVal < 0) pctVal = 0;
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", pctVal);
        const int pctW = (int)canvas_.textWidth(pct);
        const int clockRight = kScreenW / 2 + (int)canvas_.textWidth(clock) / 2;
        const int slotRight = kScreenW - 6 - badgeW - 6;  // 徽标左边留 6px
        const int textLeft = slotRight - pctW;
        const int iconW = 18, iconGap = 3;
        const bool withIcon = (textLeft - iconGap - iconW) > (clockRight + 6);
        if (textLeft <= clockRight + 4) return;  // 实在挤不下就不显示，别压到时钟上

        const uint16_t battColor = vm.batteryCharging ? kAccentSoft
                                   : pctVal <= 15          ? kWarn
                                   : pctVal <= 40          ? kPeak
                                                           : kValley;
        canvas_.setTextDatum(textdatum_t::middle_right);
        canvas_.setTextColor(battColor);
        canvas_.drawString(pct, slotRight, kStatusH / 2);
        if (withIcon) batteryIcon(textLeft - iconGap - iconW, kStatusH / 2 - 4, iconW, 8, pctVal,
                                  vm.batteryCharging, battColor);
    }
}

void Ui::batteryIcon(int x, int y, int w, int h, int pct, bool charging, uint16_t color) {
    canvas_.drawRoundRect(x, y, w, h, 2, color);
    canvas_.fillRect(x + w, y + h / 2 - 2, 2, 4, color);  // 正极凸点
    const int innerW = w - 4;
    if (charging) {  // 充电时填一条斜杠，和「满电」区分开
        canvas_.fillRect(x + 2, y + h / 2 - 1, innerW, 2, color);
        return;
    }
    int fill = (pct * innerW + 50) / 100;
    if (fill < 1 && pct > 0) fill = 1;
    if (fill > 0) canvas_.fillRect(x + 2, y + 2, fill, h - 4, color);
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

    // 右侧：余额做成「标签一行 + 大号数值一行」的块（能占满整列宽度，字号才放得大），
    // 下面两行仍是「左标签 + 右数值」；剩下的竖直空间均分成两个间隙，把整列撑满。
    canvas_.setFont(F.mono);
    const int hMono = (int)canvas_.fontHeight();

    constexpr int kColW = kColRight - kColX;
    const int colTop = 18;
    const int avail = (kFooterY - 2) - colTop;
    const int hLabel = hMono;  // 余额标签也用 mono 档，比 small 大一档

    // 余额数值：从大号字开始挑，放不下就退一档，再放不下退 mono。
    // （18pt 下 "110.00" 104px、"1234.56" 123px 都还行，"99999.99" 142px 就得退）
    char value[40];
    const char* balanceText;
    uint16_t balanceColor;
    if (vm.fetching && !vm.haveBalance) {
        balanceText = "......";
        balanceColor = kMuted;
    } else if (vm.haveBalance) {
        fmtMoney(vm.total, value, sizeof(value));
        balanceText = value;
        balanceColor = kText;
    } else {
        balanceText = lang::t(lang::Str::NoData);
        balanceColor = kWarn;
    }

    const lgfx::IFont* vf = F.value;
    float vs = F.valueScale;
    canvas_.setFont(vf);
    canvas_.setTextSize(vs);
    if ((int)canvas_.textWidth(balanceText) > kColW) {
        vf = F.valueAlt;
        vs = F.valueAltScale;
        canvas_.setFont(vf);
        canvas_.setTextSize(vs);
        if ((int)canvas_.textWidth(balanceText) > kColW) {
            vf = F.mono;
            vs = 1.0f;
            canvas_.setFont(vf);
            canvas_.setTextSize(vs);
        }
    }
    const int hValue = (int)canvas_.fontHeight();
    canvas_.setTextSize(1.0f);

    // 竖直方向：余额块 + 两行，剩余空间均分成两个间隙（两种语言都正好撑满一列）
    const int hBlock = hLabel + hValue + 1;
    int free = avail - (hBlock + hMono + hMono);
    if (free < 6) free = 6;
    const int gap = free / 2;
    int y = colTop;

    // —— 余额块：标签在左上，数值**右对齐**压在块的最下面一行 ——
    canvas_.setFont(F.mono);
    canvas_.setTextColor(kMuted);
    canvas_.setTextDatum(textdatum_t::top_left);
    canvas_.drawString(lang::t(lang::Str::Balance), kColX, y);

    canvas_.setFont(vf);
    canvas_.setTextSize(vs);
    canvas_.setTextColor(balanceColor);
    canvas_.setTextDatum(textdatum_t::top_right);
    canvas_.drawString(balanceText, kColRight, y + hLabel + 1);
    canvas_.setTextSize(1.0f);
    y += hBlock + gap;

    // —— 今日已用 ——
    canvas_.setFont(F.small);
    canvas_.setTextColor(kMuted);
    canvas_.setTextDatum(textdatum_t::middle_left);
    canvas_.drawString(lang::t(lang::Str::TodayUsed), kColX, y + hMono / 2);

    canvas_.setFont(F.mono);
    canvas_.setTextDatum(textdatum_t::middle_right);
    if (vm.today.valid) {
        canvas_.setTextColor(vm.today.partialDay ? kAccentSoft : kText);
        fmtMoney(vm.today.amount, value, sizeof(value));
    } else {
        canvas_.setTextColor(kMuted);
        snprintf(value, sizeof(value), "%s", lang::t(lang::Str::NoData));
    }
    canvas_.drawString(value, kColRight, y + hMono / 2);
    y += hMono + gap;

    // —— 峰谷档位 + 下一切换倒计时 ——
    if (vm.timeSynced && vm.nextChangeAt > vm.nowUtc) {
        canvas_.setFont(F.small);
        canvas_.setTextColor(vm.peak ? kPeak : kValley);
        canvas_.setTextDatum(textdatum_t::middle_left);
        canvas_.drawString(lang::t(vm.peak ? lang::Str::PeakPrice : lang::Str::OffPeakPrice), kColX,
                          y + hMono / 2);

        char cd[24];
        fmtCountdown(vm.nextChangeAt - vm.nowUtc, cd, sizeof(cd));
        canvas_.setFont(F.mono);
        canvas_.setTextColor(kMuted);
        canvas_.setTextDatum(textdatum_t::middle_right);
        canvas_.drawString(cd, kColRight, y + hMono / 2);
    } else {
        // 还没对时：只显示「等待对时」，不画标签（否则两段文字会挤在一起）
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
