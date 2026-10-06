#include "bubbles.h"

#include <esp_random.h>
#include <stdio.h>

#include "money.h"
#include "pricing.h"

namespace bubbles {
namespace {

constexpr uint32_t kQueueResetMs = 30000;  // 30s 不点就回到第 1 项

// 默认台词（英文；上游的台词是中文，按设备 UI 语言换了一套等价的语气）
struct Line {
    const char* text;
    uint8_t weight;
};
const Line kLines[] = {
    {"Feed me tokens, I'll keep watch.", 3},
    {"Off-peak is half price. Patience pays.", 3},
    {"Your wallet is safe with me. Probably.", 2},
    {"Peak hours: 09-12 and 14-18, Beijing time.", 3},
    {"Weekends are all off-peak now. Enjoy.", 2},
    {"Every token counted, none forgotten.", 2},
    {"Recharge? I'll note it, not spend it.", 1},
    {"M5Cardputer ADV, standing by.", 2},
    {"Blub. Balance is a state of mind.", 1},
    {"I only eat numbers.", 1},
};
constexpr size_t kLineCount = sizeof(kLines) / sizeof(kLines[0]);

int step_ = 0;
uint32_t lastAt_ = 0;
size_t lastLine_ = SIZE_MAX;

size_t pickWeighted() {
    uint16_t total = 0;
    for (const auto& l : kLines) total += l.weight;
    uint16_t r = (uint16_t)(esp_random() % total);
    for (size_t i = 0; i < kLineCount; ++i) {
        if (r < kLines[i].weight) return i;
        r -= kLines[i].weight;
    }
    return 0;
}

size_t pickLine() {
    size_t idx = pickWeighted();
    if (idx == lastLine_ && kLineCount > 1) idx = (idx + 1) % kLineCount;  // 不连续重复
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
    Bubble b;
    b.title = "WHALE SAYS";
    b.lines.push_back(kLines[pickLine()].text);
    return b;
}

Bubble next(const ViewModel& vm) {
    ensureFresh();
    Bubble b;
    char buf[64];

    switch (step_++) {
        case 0: {
            b.title = vm.haveBalance ? "BALANCE" : "NO BALANCE YET";
            if (vm.haveBalance) {
                money::format(vm.total, buf, sizeof(buf), 2);
                b.lines.push_back(std::string(vm.currency) + " " + buf + " total");
                if (vm.granted > 0) {
                    char g[32];
                    money::format(vm.granted, g, sizeof(g), 2);
                    b.lines.push_back(std::string("granted  ") + g);
                }
                if (vm.toppedUp > 0) {
                    char t[32];
                    money::format(vm.toppedUp, t, sizeof(t), 2);
                    b.lines.push_back(std::string("topped up ") + t);
                }
            } else {
                b.lines.push_back(vm.wifiOnline ? "waiting for the API" : "no WiFi yet");
            }
            break;
        }
        case 1: {
            b.title = "TODAY USED";
            if (vm.today.valid) {
                money::format(vm.today.amount, buf, sizeof(buf), 2);
                b.lines.push_back(std::string(vm.currency) + " " + buf + " observed");
                money::format(vm.today.decrease, buf, sizeof(buf), 2);
                b.lines.push_back(std::string("decrease  ") + buf);
                if (vm.today.increase > 0) {
                    money::format(vm.today.increase, buf, sizeof(buf), 2);
                    b.lines.push_back(std::string("top-up    ") + buf);
                }
                b.lines.push_back(vm.today.partialDay ? "(partial data today)" : "(full day observed)");
            } else {
                b.lines.push_back("no observation yet today");
                b.lines.push_back("the first fetch is the baseline");
            }
            break;
        }
        case 2: {
            b.title = "PRICE TIER";
            b.lines.push_back(vm.peak ? "PEAK now (x2 price)" : "OFF-PEAK now (half price)");
            if (vm.timeSynced && vm.nextChangeAt > vm.nowUtc) {
                const int64_t d = vm.nextChangeAt - vm.nowUtc;
                snprintf(buf, sizeof(buf), "switches in %lldm %02llds", (long long)(d / 60),
                         (long long)(d % 60));
                b.lines.push_back(buf);
            }
            const pricing::PriceTier& t = pricing::priceFor("deepseek-flash");
            snprintf(buf, sizeof(buf), "flash out %.2f / %.2f CNY", t.out[0], t.out[1]);
            b.lines.push_back(buf);
            b.lines.push_back("per million tokens");
            break;
        }
        default:
            b = random_(vm);
            break;
    }
    return b;
}

}  // namespace bubbles
