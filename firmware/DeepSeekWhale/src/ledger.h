// 记账内核：上游 lib/accounting.mjs 的 C++ 移植。
//
// 核心思想（与上游一致）：余额接口只给「快照」，不是流水。
// 所以把每次观测到的余额下降累加为「已观测消费」，余额上升单独记为「充值/赠金」，
// 两者互不冲抵；金额按 1e-8 元定点累计，避免浮点漂移。
//
// 按「密钥指纹 + 币种」分本（换 API key = 换一本账，旧本保留不删、不跨本相加）。
#pragma once

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

namespace ledger {

struct DayRow {
    int64_t firstAt = 0;  // 当天第一次观测
    int64_t lastAt = 0;   // 当天最后一次观测
    int64_t opening = 0;  // 当天起点余额（定点）
    int64_t last = 0;     // 最近一次余额（定点）
    int64_t debit = 0;    // 已观测消费（余额下降累计）
    int64_t credit = 0;   // 已观测充值/赠金（余额上升累计）
};

struct Book {
    std::string currency = "CNY";
    int64_t lastAt = 0;
    std::map<std::string, DayRow> days;  // key = "YYYY-MM-DD"（北京时间）
};

struct Summary {
    bool valid = false;
    std::string day;
    int64_t amount = 0;   // 已观测消费
    int64_t opening = 0;  // 统计起点余额
    int64_t current = 0;  // 当前余额（最近一次观测）
    int64_t decrease = 0;
    int64_t increase = 0;
    int64_t firstAt = 0;
    int64_t lastAt = 0;
    bool partialDay = true;  // 观测有缺口（当天不是从 00:00 开始记的）
};

class Ledger {
public:
    // scope = 密钥指纹（见 scopeFromKey），currency 一般 "CNY"。
    void begin(const std::string& scope, const std::string& currency);

    // 记一次余额观测。返回 false 表示样本被忽略（乱序/重复/参数非法）。
    bool observe(int64_t atUtc, int64_t balanceUnits, const std::string& currency);

    Summary summarizeDay(const std::string& day, int64_t nowUtc) const;
    Summary today(int64_t atUtc) const;

    // 倒序返回最近 limit 天（只含当前本）。
    std::vector<std::pair<std::string, int64_t>> recentDays(size_t limit) const;

    void prune(int keepDays, int64_t nowUtc);

    bool loadFromFile(const char* path);
    bool saveToFile(const char* path) const;
    void clear();

    const std::string& activeScope() const { return active_; }
    size_t bookCount() const { return books_.size(); }
    size_t dayCount() const;
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }
    std::string firstDay() const;

private:
    Book* activeBook();
    const Book* activeBook() const;

    std::string active_;
    std::map<std::string, Book> books_;
    bool dirty_ = false;
};

// API key -> 稳定指纹（FNV-1a 64bit，16 位十六进制）。空 key 返回 "nokey"。
std::string scopeFromKey(const std::string& apiKey);

}  // namespace ledger
