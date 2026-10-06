// 峰谷定价与时段判定：从上游 lib/index.js 的 PRICING / PEAK_HOURS /
// HOLIDAY_VALLEY / isPeakTime / nextPeakChangeAt 逐条移植。
//
// 规则（DeepSeek 官方口径，2026-09-19 版）：
//   高峰 = 北京时间 **周一至周五**（不含中国法定节假日）9:00–12:00、14:00–18:00；
//   其余时段（周末、调休上班的周末、法定节假日全天）一律空闲价。
//   空闲价 = 高峰价的一半。
#pragma once

#include <stdint.h>

namespace pricing {

// [0] = 空闲时段价，[1] = 高峰时段价，单位：元 / 百万 token
struct PriceTier {
    const char* model;
    double hit[2];
    double miss[2];
    double out[2];
};

// 按模型名子串匹配（长键优先），与上游 priceFor() 一致；未命中回落到 Flash 价。
const PriceTier& priceFor(const char* model);

// 把价钱表里的 index 取出来：0 = 空闲，1 = 高峰。
int tierIndex(int64_t utcSec);

// 北京时间（UTC+8）的日历分解。epoch 为 0 表示时间尚未同步。
struct BeijingTime {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    int dow = 4;        // 0 = 周日, 6 = 周六
    char date[11] = {}; // "YYYY-MM-DD"
    bool valid = false;
};

BeijingTime beijing(int64_t utcSec);
bool isHolidayValley(const char* yyyy_mm_dd);

// 北京时间的年月日时分秒 -> UTC epoch（不依赖 libc 的 timegm）。
// 记账内核用它把「北京时间日期」还原成当天 00:00 的 epoch，单元测试也用这个造样本。
int64_t epochFromBeijing(int year, int month, int day, int hour = 0, int minute = 0, int second = 0);

// 公历 -> 从 1970-01-01 起的天数（Howard Hinnant 的 days_from_civil）。
int64_t daysFromCivil(int year, unsigned month, unsigned day);

// 该时刻是否处于高峰计费时段。
bool isPeakTime(int64_t utcSec);

// 下一个峰谷切换时刻（epoch 秒）；扫描 12 天，找不到返回 0。
int64_t nextPeakChangeAt(int64_t utcSec);

// 按真实 usage 估算金额（定点单位）。设备端拿不到 DSH 的会话事件，
// 这个接口是给「本地估算」页和未来的用量功能留的口子。
int64_t estimateCostUnits(const char* model, int64_t hitTokens, int64_t missTokens,
                          int64_t outTokens, int64_t utcSec);

}  // namespace pricing
