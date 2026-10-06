#include "pricing.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "money.h"

namespace pricing {
namespace {

// Flash（正式模型名 deepseek-flash = DeepSeek-V4.1-Flash）
constexpr PriceTier kFlash = {"deepseek-flash", {0.02, 0.04}, {1.0, 2.0}, {4.0, 8.0}};
// Pro 为 Flash 的 3 倍价（官方 2026-08-17 生效）
constexpr PriceTier kPro = {"deepseek-v4-pro", {0.15, 0.30}, {4.5, 9.0}, {13.5, 27.0}};

// 旧模型名：请求实际由 V4.1-Flash 提供，按 Flash 价计费。
constexpr PriceTier kFlashLegacy1 = {"deepseek-v4-flash", {0.02, 0.04}, {1.0, 2.0}, {4.0, 8.0}};
constexpr PriceTier kFlashLegacy2 = {"deepseek-v4-flash-vision-exp", {0.02, 0.04}, {1.0, 2.0}, {4.0, 8.0}};

// 高峰小时段（北京时间，[起, 止)）
constexpr int kPeakHours[][2] = {{9, 12}, {14, 18}};

// 2026-08-23 00:00（北京时间）起周末全天谷价
constexpr int64_t kWeekendValleyFromSec = 1787414400LL;  // 2026-08-23T00:00+08:00
// 2026-09-19 00:00（北京时间）起法定节假日全天谷价
constexpr int64_t kHolidayValleyFromSec = 1789747200LL;  // 2026-09-19T00:00+08:00

// ===== 法定节假日全天谷价 =====
// 依据《国务院办公厅关于 2026 年部分节假日安排的通知》（国办发明电〔2025〕7 号）
// + DeepSeek API 官方计费脚注。只列**放假**的日期：调休上班日全部落在周末
// （2026 年为 1/4、2/14、2/28、5/9、9/20、10/10），按「周末也是谷价」本来就免费。
//
// ⚠️ 每年 11 月国务院发布次年安排后，必须在这里补下一年的日期。
//    CI 里的 tools/check-holidays.py 会在当年已过去一半、而表里没有下一年时告警。
const char* const kHolidayValley[] = {
    "2026-01-01", "2026-01-02", "2026-01-03",                          // 元旦
    "2026-02-15", "2026-02-16", "2026-02-17", "2026-02-18", "2026-02-19",  // 春节
    "2026-02-20", "2026-02-21", "2026-02-22", "2026-02-23",
    "2026-04-04", "2026-04-05", "2026-04-06",                          // 清明
    "2026-05-01", "2026-05-02", "2026-05-03", "2026-05-04", "2026-05-05",  // 劳动节
    "2026-06-19", "2026-06-20", "2026-06-21",                          // 端午
    "2026-09-25", "2026-09-26", "2026-09-27",                          // 中秋
    "2026-10-01", "2026-10-02", "2026-10-03", "2026-10-04",            // 国庆
    "2026-10-05", "2026-10-06", "2026-10-07",
};
constexpr size_t kHolidayValleyCount = sizeof(kHolidayValley) / sizeof(kHolidayValley[0]);

}  // namespace

const PriceTier& priceFor(const char* model) {
    if (!model || !*model) return kFlash;
    // 上游 priceFor() 第一件事就是 toLowerCase()：模型名大小写不该影响计价。
    char lower[64];
    size_t i = 0;
    for (; model[i] != '\0' && i < sizeof(lower) - 1; ++i) {
        const char c = model[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    lower[i] = '\0';
    // 与上游一致：按键长降序匹配，避免短关键字（如 pro）误伤别的模型。
    static const PriceTier* const kTable[] = {&kFlashLegacy2, &kFlashLegacy1, &kPro, &kFlash};
    for (const PriceTier* t : kTable) {
        if (strstr(lower, t->model) != nullptr) return *t;
    }
    return kFlash;  // _default
}

// 公历 -> 天数（Howard Hinnant 的 days_from_civil）。arduino-esp32 3.x 没有 timegm()，
// 所以日期换 epoch 全部走这里，避免依赖 libc 的时区实现。
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    int y = year - (month <= 2 ? 1 : 0);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (month + (month > 2 ? -3u : 9u)) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t epochFromBeijing(int year, int month, int day, int hour, int minute, int second) {
    const int64_t days = daysFromCivil(year, (unsigned)month, (unsigned)day);
    return days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60 + second - 8 * 3600;
}

BeijingTime beijing(int64_t utcSec) {
    BeijingTime bt;
    if (utcSec <= 0) return bt;  // 时间未同步
    time_t shifted = (time_t)(utcSec + 8 * 3600);
    struct tm tmv {};
    if (gmtime_r(&shifted, &tmv) == nullptr) return bt;
    bt.year = tmv.tm_year + 1900;
    bt.month = tmv.tm_mon + 1;
    bt.day = tmv.tm_mday;
    bt.hour = tmv.tm_hour;
    bt.minute = tmv.tm_min;
    bt.second = tmv.tm_sec;
    bt.dow = tmv.tm_wday;
    // 年月日都显式夹紧：既防格式化溢出 date[11]，也挡住 tm 的异常值
    int y = bt.year, mo = bt.month, dd = bt.day;
    if (y < 0) y = 0;
    if (y > 9999) y = 9999;
    if (mo < 1) mo = 1;
    if (mo > 12) mo = 12;
    if (dd < 1) dd = 1;
    if (dd > 31) dd = 31;
    snprintf(bt.date, sizeof(bt.date), "%04d-%02d-%02d", y, mo, dd);
    bt.valid = true;
    return bt;
}

bool isHolidayValley(const char* yyyy_mm_dd) {
    if (!yyyy_mm_dd) return false;
    for (size_t i = 0; i < kHolidayValleyCount; ++i) {
        if (strcmp(kHolidayValley[i], yyyy_mm_dd) == 0) return true;
    }
    return false;
}

bool isPeakTime(int64_t utcSec) {
    if (utcSec <= 0) return false;
    const BeijingTime bt = beijing(utcSec);
    if (!bt.valid) return false;
    if (utcSec >= kWeekendValleyFromSec && (bt.dow == 0 || bt.dow == 6)) return false;
    if (utcSec >= kHolidayValleyFromSec && isHolidayValley(bt.date)) return false;
    for (const auto& span : kPeakHours) {
        if (bt.hour >= span[0] && bt.hour < span[1]) return true;
    }
    return false;
}

int tierIndex(int64_t utcSec) {
    return isPeakTime(utcSec) ? 1 : 0;
}

int64_t nextPeakChangeAt(int64_t utcSec) {
    if (utcSec <= 0) return 0;
    const bool cur = isPeakTime(utcSec);
    // 北京当日 00:00 的 epoch（把时间轴平移 +8h 后按天取整，再平移回来）
    const int64_t day0 = ((utcSec + 8 * 3600) / 86400) * 86400;
    static const int kEdges[] = {0, 9, 12, 14, 18};
    for (int d = 0; d <= 12; ++d) {
        for (int edge : kEdges) {
            const int64_t cand = day0 + (int64_t)d * 86400 + (int64_t)edge * 3600 - 8 * 3600;
            if (cand <= utcSec + 1) continue;
            if (isPeakTime(cand) != cur) return cand;
        }
    }
    return 0;
}

int64_t estimateCostUnits(const char* model, int64_t hitTokens, int64_t missTokens,
                          int64_t outTokens, int64_t utcSec) {
    const PriceTier& t = priceFor(model);
    const int idx = tierIndex(utcSec);
    const double cny = ((double)hitTokens * t.hit[idx] + (double)missTokens * t.miss[idx] +
                        (double)outTokens * t.out[idx]) /
                       1e6;
    return money::fromDouble(cny);
}

}  // namespace pricing
