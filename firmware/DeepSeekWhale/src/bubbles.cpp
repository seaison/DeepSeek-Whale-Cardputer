#include "bubbles.h"

#include <esp_random.h>
#include <stdio.h>

#include "lang.h"
#include "money.h"
#include "pricing.h"

namespace bubbles {
namespace {

constexpr uint32_t kQueueResetMs = 30000;  // 30s 不点就回到第 1 项

// 台词表在 lang.cpp（中英各一套，一一对应），这里只负责抽签。
// 按当前语言重新求值，切语言后立刻生效。

int step_ = 0;
uint32_t lastAt_ = 0;
size_t lastLine_ = SIZE_MAX;

size_t pickWeighted() {
    size_t count = 0;
    const lang::BubbleLine* lines = lang::bubbleLines(count);
    uint16_t total = 0;
    for (size_t i = 0; i < count; ++i) total += lines[i].weight;
    if (total == 0) return 0;
    uint16_t r = (uint16_t)(esp_random() % total);
    for (size_t i = 0; i < count; ++i) {
        if (r < lines[i].weight) return i;
        r -= lines[i].weight;
    }
    return 0;
}

size_t pickLine() {
    size_t count = 0;
    lang::bubbleLines(count);
    size_t idx = pickWeighted();
    if (idx == lastLine_ && count > 1) idx = (idx + 1) % count;  // 不连续重复
    lastLine_ = idx;
    return idx;
}

}  // namespace

void reset() {
    step_ = 0;
    lastAt_ = 0;
}

bool active() {
    return lastAt_ != 0;
}

void touch() {
    lastAt_ = millis();
}

static void ensureFresh() {
    const uint32_t now = millis();
    if (lastAt_ == 0 || now - lastAt_ > kQueueResetMs) step_ = 0;
    lastAt_ = now;
}

Bubble random_(const ViewModel& vm) {
    (void)vm;
    size_t count = 0;
    const lang::BubbleLine* lines = lang::bubbleLines(count);
    Bubble b;
    b.title = lang::t(lang::Str::BubbleWhaleSays);
    b.lines.push_back(lines[pickLine()].text);
    return b;
}

Bubble next(const ViewModel& vm) {
    ensureFresh();
    Bubble b;
    char buf[64];

    switch (step_++) {
        case 0: {
            b.title = lang::t(vm.haveBalance ? lang::Str::BubbleBalance : lang::Str::BubbleNoBalance);
            if (vm.haveBalance) {
                money::format(vm.total, buf, sizeof(buf), 2);
                b.lines.push_back(std::string(vm.currency) + " " + buf +
                                  lang::t(lang::Str::BubbleTotalSuffix));
                if (vm.granted > 0) {
                    char g[32];
                    money::format(vm.granted, g, sizeof(g), 2);
                    b.lines.push_back(std::string(lang::t(lang::Str::BubbleGrantedPrefix)) + g);
                }
                if (vm.toppedUp > 0) {
                    char t[32];
                    money::format(vm.toppedUp, t, sizeof(t), 2);
                    b.lines.push_back(std::string(lang::t(lang::Str::BubbleToppedPrefix)) + t);
                }
            } else {
                b.lines.push_back(lang::t(vm.wifiOnline ? lang::Str::BubbleWaitApi
                                                        : lang::Str::BubbleNoWifi));
            }
            break;
        }
        case 1: {
            b.title = lang::t(lang::Str::BubbleToday);
            if (vm.today.valid) {
                money::format(vm.today.amount, buf, sizeof(buf), 2);
                b.lines.push_back(std::string(vm.currency) + " " + buf +
                                  lang::t(lang::Str::BubbleObservedSuffix));
                money::format(vm.today.decrease, buf, sizeof(buf), 2);
                b.lines.push_back(std::string(lang::t(lang::Str::BubbleDecreasePrefix)) + buf);
                if (vm.today.increase > 0) {
                    money::format(vm.today.increase, buf, sizeof(buf), 2);
                    b.lines.push_back(std::string(lang::t(lang::Str::BubbleIncreasePrefix)) + buf);
                }
                b.lines.push_back(lang::t(vm.today.partialDay ? lang::Str::BubblePartial
                                                             : lang::Str::BubbleFullDay));
            } else {
                b.lines.push_back(lang::t(lang::Str::BubbleNoObservation));
                b.lines.push_back(lang::t(lang::Str::BubbleBaseline));
            }
            break;
        }
        case 2: {
            b.title = lang::t(lang::Str::BubblePriceTier);
            b.lines.push_back(lang::t(vm.peak ? lang::Str::BubblePeakNow : lang::Str::BubbleValleyNow));
            if (vm.timeSynced && vm.nextChangeAt > vm.nowUtc) {
                const int64_t d = vm.nextChangeAt - vm.nowUtc;
                snprintf(buf, sizeof(buf), lang::t(lang::Str::BubbleSwitchFmt), (long long)(d / 60),
                         (long long)(d % 60));
                b.lines.push_back(buf);
            }
            const pricing::PriceTier& t = pricing::priceFor("deepseek-flash");
            snprintf(buf, sizeof(buf), lang::t(lang::Str::BubbleFlashPriceFmt), t.out[0], t.out[1]);
            b.lines.push_back(buf);
            b.lines.push_back(lang::t(lang::Str::BubblePerMillion));
            break;
        }
        default:
            b = random_(vm);
            break;
    }
    return b;
}

}  // namespace bubbles
