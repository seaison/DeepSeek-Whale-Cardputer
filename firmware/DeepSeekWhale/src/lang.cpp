#include "lang.h"

#include <string.h>

namespace lang {
namespace {

Lang s_lang = Lang::En;

// 两张表由同一份 DSW_STRINGS 生成 ⇒ 字段数天然一致，漏翻不可能悄悄发生。
const char* const kEn[] = {
#define X(name, en, zh) en,
    DSW_STRINGS(X)
#undef X
};

const char* const kZh[] = {
#define X(name, en, zh) zh,
    DSW_STRINGS(X)
#undef X
};

static_assert(sizeof(kEn) / sizeof(kEn[0]) == (size_t)Str::Count, "英文表与枚举不一致");
static_assert(sizeof(kZh) / sizeof(kZh[0]) == (size_t)Str::Count, "中文表与枚举不一致");

// 英文：点阵 + FreeSans 矢量，紧凑、数字好看
constexpr FontSet kFontsEn = {
    &fonts::Font0,             // 6x8
    &fonts::AsciiFont8x16,     // 8x16
    &fonts::FreeSans12pt7b,    // 余额数值（yAdvance 29，比原来的 9pt/22 大一档）
    &fonts::Orbitron_Light_24, // 标题
    1.0f
};

// 中文：efontCN 子集字库（U8g2）。三档字号合计约 +1.08MB flash，见 docs/ARCHITECTURE.md
constexpr FontSet kFontsZh = {
    &fonts::efontCN_12,  // 小标签
    &fonts::efontCN_16,  // 菜单 / 列表 / 余额标签
    &fonts::efontCN_24,  // 主数值（efont 里最大的 CN 字号）
    &fonts::efontCN_24,  // 标题
    1.15f                // 主数值再放大一点，和英文那档的视觉大小对齐
};

// ===== 随机台词 =====
// 权重 + 两种语言；中英各 10 条，一一对应（改文案时两列一起改）。
struct BubblePair {
    uint8_t weight;
    const char* en;
    const char* zh;
};

const BubblePair kBubblePairs[] = {
    {3, "Feed me tokens, I'll keep watch.", "喂我点 token，我替你看家。"},
    {3, "Off-peak is half price. Patience pays.", "空闲时段半价，等等更划算。"},
    {2, "Your wallet is safe with me. Probably.", "你的钱包交给我，大概安全。"},
    {3, "Peak hours: 09-12 and 14-18, Beijing time.", "高峰时段：北京时间 9-12、14-18。"},
    {2, "Weekends are all off-peak now. Enjoy.", "现在周末全天谷价，随便用。"},
    {2, "Every token counted, none forgotten.", "每个 token 都记账，一个不落。"},
    {1, "Recharge? I'll note it, not spend it.", "充值我只记一笔，不花。"},
    {2, "M5Cardputer ADV, standing by.", "M5Cardputer ADV，待命中。"},
    {1, "Blub. Balance is a state of mind.", "咕噜。余额是一种心态。"},
    {1, "I only eat numbers.", "我只吃数字。"},
};
constexpr size_t kBubbleCount = sizeof(kBubblePairs) / sizeof(kBubblePairs[0]);

BubbleLine s_linesEn[kBubbleCount];
BubbleLine s_linesZh[kBubbleCount];
bool s_linesBuilt = false;

void buildLines() {
    if (s_linesBuilt) return;
    for (size_t i = 0; i < kBubbleCount; ++i) {
        s_linesEn[i] = {kBubblePairs[i].weight, kBubblePairs[i].en};
        s_linesZh[i] = {kBubblePairs[i].weight, kBubblePairs[i].zh};
    }
    s_linesBuilt = true;
}

}  // namespace

Lang current() {
    return s_lang;
}

void set(Lang l) {
    s_lang = l;
}

bool isCJK() {
    return s_lang == Lang::Zh;
}

const char* t(Str s) {
    const size_t idx = (size_t)s;
    if (idx >= (size_t)Str::Count) return "?";
    return isCJK() ? kZh[idx] : kEn[idx];
}

const char* code() {
    return isCJK() ? "zh" : "en";
}

Lang fromCode(const char* c, Lang fallback) {
    if (!c || !*c) return fallback;
    if (strcmp(c, "zh") == 0 || strcmp(c, "zh-CN") == 0 || strcmp(c, "cn") == 0) return Lang::Zh;
    if (strcmp(c, "en") == 0 || strcmp(c, "en-US") == 0) return Lang::En;
    return fallback;
}

const FontSet& fonts() {
    return isCJK() ? kFontsZh : kFontsEn;
}

const BubbleLine* bubbleLines(size_t& count) {
    buildLines();
    if (isCJK()) {
        count = kBubbleCount;
        return s_linesZh;
    }
    count = kBubbleCount;
    return s_linesEn;
}

}  // namespace lang
