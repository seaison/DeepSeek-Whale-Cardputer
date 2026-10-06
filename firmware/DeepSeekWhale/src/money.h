// 定点金额工具：与上游 DSH 插件的记账内核口径一致（SCALE = 1e8）。
// 所有金额在内部一律用 int64 的「1e-8 元」单位表示，避免浮点累计误差。
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>  // strtod（主机侧单测直接编译本头文件时需要）

namespace money {

// 1 元 = 100000000 单位（上游 accounting.mjs 的 SCALE）。
constexpr int64_t SCALE = 100000000LL;
constexpr int64_t SCALE_HALF = SCALE / 2;

inline int64_t fromDouble(double v) {
    return (int64_t)(v >= 0 ? (v * (double)SCALE + 0.5) : (v * (double)SCALE - 0.5));
}

inline double toDouble(int64_t units) {
    return (double)units / (double)SCALE;
}

// 解析 "110.00" / "1e2" / "-3.5" 这类字符串，失败返回 false。
inline bool parse(const char* s, int64_t& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    double v = strtod(s, &end);
    if (end == s || v != v) return false;
    out = fromDouble(v);
    return true;
}

// 格式化成固定小数位（默认两位），四舍五入。
inline void format(int64_t units, char* buf, size_t n, int decimals = 2) {
    if (!buf || n == 0) return;
    bool neg = units < 0;
    uint64_t abs = neg ? (uint64_t)(-units) : (uint64_t)units;
    uint64_t ip = abs / (uint64_t)SCALE;
    uint64_t fp = abs % (uint64_t)SCALE;
    static const uint64_t pow10[] = {1ULL, 10ULL, 100ULL, 1000ULL, 10000ULL,
                                     100000ULL, 1000000ULL, 10000000ULL, 100000000ULL};
    if (decimals < 0) decimals = 0;
    if (decimals > 8) decimals = 8;
    uint64_t div = pow10[8 - decimals];
    uint64_t frac = (fp + div / 2) / div;
    uint64_t frac_max = pow10[decimals];
    if (frac >= frac_max) {  // 进位到整数部分
        ip += 1;
        frac = 0;
    }
    if (decimals == 0) {
        snprintf(buf, n, "%s%llu", neg ? "-" : "", (unsigned long long)ip);
    } else {
        snprintf(buf, n, "%s%llu.%0*llu", neg ? "-" : "", (unsigned long long)ip, decimals,
                 (unsigned long long)frac);
    }
}

}  // namespace money
