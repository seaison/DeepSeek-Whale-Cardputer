// 主机侧单元测试：不需要 ESP32、不需要 Arduino，`tests/run.sh` 直接编译运行。
//
// 覆盖两块**纯逻辑**（也是最容易出错、且出错就记错账的部分）：
//   1. money.h  —— 1e-8 定点金额的解析与格式化（含四舍五入进位）
//   2. pricing   —— 峰谷判定 / 下一切换时刻 / 北京时间换算 / 价目表
//
// 其中峰谷判定是拿 tests/golden/peak-2026.tsv 对拍——那份 golden 是由
// **上游 lib/index.js 的原始实现**生成的（见 tests/gen-golden.mjs），
// 所以这个测试验证的是「本移植与上游等价」，而不是「和自己一致」。
//
// 构建与运行见 tests/run.sh；CI 里由 native-tests job 跑。

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "../../firmware/DeepSeekWhale/src/money.h"
#include "../../firmware/DeepSeekWhale/src/pricing.h"

namespace {

int g_pass = 0;
int g_fail = 0;
std::string g_section;

void section(const char* name) {
    g_section = name;
    printf("\n== %s ==\n", name);
}

void check(bool ok, const char* what, const std::string& detail = "") {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        printf("  FAIL [%s] %s%s%s\n", g_section.c_str(), what, detail.empty() ? "" : "  -> ",
               detail.c_str());
    }
}

void checkEq(long long got, long long want, const char* what) {
    char buf[128];
    snprintf(buf, sizeof(buf), "got %lld, want %lld", got, want);
    check(got == want, what, buf);
}

void checkStr(const char* got, const char* want, const char* what) {
    std::string d = std::string("got \"") + got + "\", want \"" + want + "\"";
    check(strcmp(got, want) == 0, what, d);
}

// ---------------- money ----------------
void testMoney() {
    section("money：定点金额");

    int64_t v = 0;
    check(money::parse("110.00", v), "parse 110.00 成功");
    checkEq(v, 11000000000LL, "parse 110.00");
    check(money::parse("0.00000001", v), "parse 最小单位");
    checkEq(v, 1LL, "parse 0.00000001 = 1 单位");
    check(money::parse("-3.5", v), "parse 负数");
    checkEq(v, -350000000LL, "parse -3.5");
    check(!money::parse("", v), "parse 空串应失败");
    check(!money::parse("abc", v), "parse 非数字应失败");
    check(!money::parse(nullptr, v), "parse nullptr 应失败");

    char buf[32];
    money::format(11000000000LL, buf, sizeof(buf), 2);
    checkStr(buf, "110.00", "format 110.00");
    money::format(0, buf, sizeof(buf), 2);
    checkStr(buf, "0.00", "format 0");
    money::format(1, buf, sizeof(buf), 2);
    checkStr(buf, "0.00", "format 最小单位四舍五入到 0.00");
    money::format(5000000, buf, sizeof(buf), 2);
    checkStr(buf, "0.05", "format 0.05");
    // 进位：9.999 -> 10.00
    money::format(999900000LL, buf, sizeof(buf), 2);
    checkStr(buf, "10.00", "format 9.999 进位到 10.00");
    money::format(-123456789LL, buf, sizeof(buf), 2);
    checkStr(buf, "-1.23", "format 负数");
    money::format(11000000000LL, buf, sizeof(buf), 0);
    checkStr(buf, "110", "format 0 位小数");
    money::format(123456789012LL, buf, sizeof(buf), 2);
    checkStr(buf, "1234.57", "format 大额");
}

// ---------------- pricing：时间换算 ----------------
void testBeijing() {
    section("pricing：北京时间换算");

    // 2026-10-06 是周二
    const int64_t t = pricing::epochFromBeijing(2026, 10, 6, 12, 0, 0);
    const pricing::BeijingTime bt = pricing::beijing(t);
    check(bt.valid, "beijing() 有效");
    checkEq(bt.year, 2026, "year");
    checkEq(bt.month, 10, "month");
    checkEq(bt.day, 6, "day");
    checkEq(bt.hour, 12, "hour");
    checkEq(bt.dow, 2, "周二");
    checkStr(bt.date, "2026-10-06", "date 字符串");

    // 跨零点：北京时间 00:00 对应 UTC 前一天 16:00
    const int64_t midnight = pricing::epochFromBeijing(2026, 10, 6, 0, 0, 0);
    checkEq((midnight + 8 * 3600) % 86400, 0, "北京 00:00 对齐");
    checkEq(pricing::beijing(midnight + 86399).day, 6, "当天最后一秒仍属 6 号");
    checkEq(pricing::beijing(midnight - 1).day, 5, "前一天最后一秒属 5 号");

    // 星期几（用已知的日子交叉验证）
    const int64_t known = pricing::epochFromBeijing(2026, 1, 1);  // 元旦，周四
    checkEq(pricing::beijing(known).dow, 4, "2026-01-01 是周四");
    const int64_t d2024 = pricing::epochFromBeijing(2024, 2, 29);  // 闰日
    checkEq(pricing::beijing(d2024).day, 29, "2024-02-29 存在");
    checkEq(pricing::beijing(pricing::epochFromBeijing(2026, 12, 31)).year, 2026, "年末不越界");
    check(!pricing::beijing(0).valid, "epoch 0 视为未同步");
}

// ---------------- pricing：价目表 ----------------
void testPriceTable() {
    section("pricing：价目表");

    const pricing::PriceTier& flash = pricing::priceFor("deepseek-flash");
    checkStr(flash.model, "deepseek-flash", "flash 命中");
    check(flash.hit[0] == 0.02 && flash.hit[1] == 0.04, "flash 缓存命中价");
    check(flash.miss[0] == 1.0 && flash.miss[1] == 2.0, "flash 未命中价");
    check(flash.out[0] == 4.0 && flash.out[1] == 8.0, "flash 输出价");

    const pricing::PriceTier& pro = pricing::priceFor("deepseek-v4-pro");
    checkStr(pro.model, "deepseek-v4-pro", "pro 命中");
    check(pro.out[1] == 27.0, "pro 输出高峰价");

    // 旧模型名回落 Flash 价（上游同样如此）
    checkStr(pricing::priceFor("deepseek-v4-flash-vision-exp").model,
             "deepseek-v4-flash-vision-exp", "旧名命中");
    check(pricing::priceFor("deepseek-v4-flash").miss[0] == 1.0, "旧名按 Flash 价");
    checkStr(pricing::priceFor("some-unknown-model").model, "deepseek-flash", "未知模型回落 flash");
    checkStr(pricing::priceFor(nullptr).model, "deepseek-flash", "nullptr 回落 flash");
    // 长键优先：deepseek-v4-pro 不能被别的短键抢走
    checkStr(pricing::priceFor("DeepSeek-V4-Pro").model, "deepseek-v4-pro", "大小写不敏感");

    // 成本估算：空闲时段 1M 输出 token，flash = 4 元
    const int64_t valleyAt = pricing::epochFromBeijing(2026, 10, 3, 3, 0, 0);  // 周六凌晨
    checkEq(pricing::estimateCostUnits("deepseek-flash", 0, 0, 1000000, valleyAt),
            4LL * money::SCALE, "1M 输出 token 空闲时段 = 4 元");
    // 2026-10-06 是国庆假期（法定节假日全天谷价）——顺手验证节假日规则真的生效
    const int64_t holidayAt = pricing::epochFromBeijing(2026, 10, 6, 10, 0, 0);
    check(!pricing::isPeakTime(holidayAt), "国庆假期的工作时段算谷价");
    checkEq(pricing::estimateCostUnits("deepseek-flash", 0, 0, 1000000, holidayAt),
            4LL * money::SCALE, "假期内 1M 输出 token 仍按谷价 4 元");
    // 假期结束后的周四上午 = 高峰
    const int64_t peakAt = pricing::epochFromBeijing(2026, 10, 8, 10, 0, 0);
    check(pricing::isPeakTime(peakAt), "10-08 周四上午是高峰");
    checkEq(pricing::estimateCostUnits("deepseek-flash", 0, 0, 1000000, peakAt),
            8LL * money::SCALE, "1M 输出 token 高峰时段 = 8 元");
    checkEq(pricing::estimateCostUnits("deepseek-flash", 1000000, 0, 0, valleyAt),
            2LL * money::SCALE / 100, "1M 缓存命中 token 空闲 = 0.02 元");
}

// ---------------- 与上游对拍 ----------------
bool parseGoldenLine(const char* line, int* y, int* mo, int* d, unsigned* bits, long long* next) {
    if (!line || line[0] == '#' || line[0] == '\n' || line[0] == '\0') return false;
    return sscanf(line, "%d-%d-%d\t%x\t%lld", y, mo, d, bits, next) == 5;
}

int testGolden(const char* path) {
    section("pricing：与上游 golden 对拍（峰谷判定）");

    FILE* f = fopen(path, "r");
    if (!f) {
        printf("  找不到 golden 文件: %s\n", path);
        ++g_fail;
        return 1;
    }

    char line[256];
    int days = 0, hours = 0, nextChecked = 0;
    int firstFailDay = -1;
    while (fgets(line, sizeof(line), f)) {
        int y = 0, mo = 0, d = 0;
        unsigned bits = 0;
        long long next = 0;
        if (!parseGoldenLine(line, &y, &mo, &d, &bits, &next)) continue;

        const int64_t dayStart = pricing::epochFromBeijing(y, mo, d, 0, 0, 0);
        bool dayOk = true;
        for (int h = 0; h < 24; ++h) {
            const int64_t t = pricing::epochFromBeijing(y, mo, d, h, 0, 0);
            const bool want = (bits >> h) & 1u;
            const bool got = pricing::isPeakTime(t);
            if (got != want) {
                dayOk = false;
                if (firstFailDay < 0) {
                    printf("  首个不一致: %04d-%02d-%02d %02d:00 got=%d want=%d\n", y, mo, d, h,
                           (int)got, (int)want);
                    firstFailDay = days;
                }
            }
            ++hours;
        }
        const int64_t gotNext = pricing::nextPeakChangeAt(dayStart);
        if (gotNext != (int64_t)next) {
            dayOk = false;
            if (firstFailDay < 0) {
                printf("  首个 nextPeakChangeAt 不一致: %04d-%02d-%02d got=%lld want=%lld\n", y, mo,
                       d, (long long)gotNext, next);
                firstFailDay = days;
            }
        }
        ++nextChecked;
        if (!dayOk) ++g_fail;
        ++days;
    }
    fclose(f);

    printf("  对拍 %d 天 / %d 个小时样本 / %d 个切换点\n", days, hours, nextChecked);
    check(days > 350, "golden 覆盖了足够的天数");
    check(firstFailDay < 0, "全部小时与切换点与上游一致");
    // 成功的话 g_fail 里不该有本节的失败
    return g_fail;
}

}  // namespace

int main(int argc, char** argv) {
    const char* golden = argc > 1 ? argv[1] : "tests/golden/peak-2026.tsv";

    printf("DeepSeek Whale · 主机侧单元测试\n");
    printf("golden: %s\n", golden);

    testMoney();
    testBeijing();
    testPriceTable();
    testGolden(golden);

    printf("\n----------------------------------------\n");
    printf("通过 %d，失败 %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
