#include "ledger.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pricing.h"

namespace ledger {
namespace {

// 与上游 PARTIAL_GAP_MS 一致：观测起点/终点离当日边界超过 10 分钟就算「部分数据」。
constexpr int64_t kPartialGapSec = 10 * 60;

int64_t dayStartUtc(const std::string& day) {
    // day 是北京时间日期，转成对应的 UTC epoch（北京 00:00 = UTC 前一天 16:00）
    int y = 0, m = 0, d = 0;
    if (sscanf(day.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return -1;
    if (m < 1 || m > 12 || d < 1 || d > 31) return -1;
    return pricing::epochFromBeijing(y, m, d);
}

}  // namespace

std::string scopeFromKey(const std::string& apiKey) {
    if (apiKey.empty()) return "nokey";
    uint64_t h = 1469598103934665603ULL;  // FNV-1a 64 偏移
    for (unsigned char c : apiKey) {
        h ^= (uint64_t)c;
        h *= 1099511628211ULL;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

void Ledger::begin(const std::string& scope, const std::string& currency) {
    active_ = scope + "-" + currency;
    if (books_.find(active_) == books_.end()) {
        Book b;
        b.currency = currency;
        books_[active_] = b;
    } else {
        books_[active_].currency = currency;
    }
}

Book* Ledger::activeBook() {
    auto it = books_.find(active_);
    return it == books_.end() ? nullptr : &it->second;
}

const Book* Ledger::activeBook() const {
    auto it = books_.find(active_);
    return it == books_.end() ? nullptr : &it->second;
}

bool Ledger::observe(int64_t atUtc, int64_t balanceUnits, const std::string& currency) {
    if (atUtc <= 0) return false;
    if (currency.size() != 3) return false;
    if (books_.find(active_) == books_.end()) {
        Book b;
        b.currency = currency;
        books_[active_] = b;
    }
    Book& book = books_[active_];
    if (book.lastAt != 0 && atUtc <= book.lastAt) return false;  // 乱序/重复样本直接丢

    const pricing::BeijingTime bt = pricing::beijing(atUtc);
    if (!bt.valid) return false;
    const std::string day(bt.date);

    auto it = book.days.find(day);
    if (it == book.days.end()) {
        DayRow row;
        row.firstAt = row.lastAt = atUtc;
        row.opening = row.last = balanceUnits;
        book.days[day] = row;
    } else {
        DayRow& row = it->second;
        const int64_t delta = row.last - balanceUnits;
        if (delta > 0) row.debit += delta;   // 余额下降 = 消费
        if (delta < 0) row.credit -= delta;  // 余额上升 = 充值/赠金
        row.last = balanceUnits;
        row.lastAt = atUtc;
    }
    book.lastAt = atUtc;
    dirty_ = true;
    return true;
}

Summary Ledger::summarizeDay(const std::string& day, int64_t nowUtc) const {
    Summary s;
    const Book* book = activeBook();
    if (!book) return s;
    auto it = book->days.find(day);
    if (it == book->days.end()) return s;
    const DayRow& row = it->second;

    s.valid = true;
    s.day = day;
    s.opening = row.opening;
    s.current = row.last;
    s.decrease = row.debit;
    s.increase = row.credit;
    s.firstAt = row.firstAt;
    s.lastAt = row.lastAt;
    // 上游 observedAmount() 的口径：没有余额校正时，消费 = 余额下降累计。
    s.amount = row.debit;

    // 与上游 partialDayOf() 同源：观测起点离当日 00:00、或（对已经过去的日子）
    // 最后一次观测离次日 00:00 超过 10 分钟，就标记为「部分数据」。
    const int64_t start = dayStartUtc(day);
    if (start > 0) {
        const int64_t end = start + 86400;
        const int64_t beforeGap = row.firstAt - start;
        const int64_t afterGap = (nowUtc >= end) ? (end - row.lastAt) : 0;
        s.partialDay = (beforeGap > kPartialGapSec) || (afterGap > kPartialGapSec);
    }
    return s;
}

Summary Ledger::today(int64_t atUtc) const {
    const pricing::BeijingTime bt = pricing::beijing(atUtc);
    if (!bt.valid) return Summary();
    return summarizeDay(std::string(bt.date), atUtc);
}

std::vector<std::pair<std::string, int64_t>> Ledger::recentDays(size_t limit) const {
    std::vector<std::pair<std::string, int64_t>> out;
    const Book* book = activeBook();
    if (!book) return out;
    for (auto it = book->days.rbegin(); it != book->days.rend() && out.size() < limit; ++it) {
        out.emplace_back(it->first, it->second.debit);
    }
    return out;
}

std::string Ledger::firstDay() const {
    const Book* book = activeBook();
    if (!book || book->days.empty()) return std::string();
    return book->days.begin()->first;
}

size_t Ledger::dayCount() const {
    const Book* book = activeBook();
    return book ? book->days.size() : 0;
}

void Ledger::prune(int keepDays, int64_t nowUtc) {
    Book* book = activeBook();
    if (!book || keepDays <= 0) return;
    const pricing::BeijingTime bt = pricing::beijing(nowUtc);
    if (!bt.valid) return;
    // 逐天回退，算出保留的最早日期
    int64_t cutoff = nowUtc - (int64_t)keepDays * 86400;
    const pricing::BeijingTime cb = pricing::beijing(cutoff);
    if (!cb.valid) return;
    const std::string keepFrom(cb.date);
    for (auto it = book->days.begin(); it != book->days.end();) {
        if (it->first < keepFrom) {
            it = book->days.erase(it);
            dirty_ = true;
        } else {
            ++it;
        }
    }
}

void Ledger::clear() {
    books_.clear();
    active_.clear();
    dirty_ = true;
}

bool Ledger::loadFromFile(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    JsonObject root = doc.as<JsonObject>();
    if (root["v"].as<int>() != 1) return false;

    active_ = root["active"].as<const char*>() ? root["active"].as<const char*>() : "";
    books_.clear();
    JsonObject books = root["books"].as<JsonObject>();
    for (JsonPair kv : books) {
        Book b;
        JsonObject bj = kv.value().as<JsonObject>();
        b.currency = bj["cur"].as<const char*>() ? bj["cur"].as<const char*>() : "CNY";
        b.lastAt = bj["lastAt"].as<int64_t>();
        JsonObject days = bj["days"].as<JsonObject>();
        for (JsonPair dkv : days) {
            JsonObject dj = dkv.value().as<JsonObject>();
            DayRow row;
            row.firstAt = dj["f"].as<int64_t>();
            row.lastAt = dj["l"].as<int64_t>();
            row.opening = dj["o"].as<int64_t>();
            row.last = dj["u"].as<int64_t>();
            row.debit = dj["d"].as<int64_t>();
            row.credit = dj["c"].as<int64_t>();
            b.days[dkv.key().c_str()] = row;
        }
        books_[kv.key().c_str()] = b;
    }
    dirty_ = false;
    return true;
}

bool Ledger::saveToFile(const char* path) const {
    JsonDocument doc;
    doc["v"] = 1;
    doc["active"] = active_;
    JsonObject books = doc["books"].to<JsonObject>();
    for (const auto& kv : books_) {
        JsonObject bj = books[kv.first].to<JsonObject>();
        bj["cur"] = kv.second.currency;
        bj["lastAt"] = kv.second.lastAt;
        JsonObject days = bj["days"].to<JsonObject>();
        for (const auto& dkv : kv.second.days) {
            JsonObject dj = days[dkv.first].to<JsonObject>();
            dj["f"] = dkv.second.firstAt;
            dj["l"] = dkv.second.lastAt;
            dj["o"] = dkv.second.opening;
            dj["u"] = dkv.second.last;
            dj["d"] = dkv.second.debit;
            dj["c"] = dkv.second.credit;
        }
    }
    // 先写临时文件再改名：避免掉电/拔卡把账本写坏（上游也是原子写入）。
    const std::string tmp = std::string(path) + ".tmp";
    File f = SD.open(tmp.c_str(), FILE_WRITE);
    if (!f) return false;
    const size_t written = serializeJson(doc, f);
    f.flush();
    f.close();
    if (written == 0) return false;
    SD.remove(path);
    if (!SD.rename(tmp.c_str(), path)) {
        // 改名失败时数据还在 .tmp 里，没有丢——但账本位置不对，需要人工处理
        Serial.printf("[ledger] rename 失败: %s -> %s\n", tmp.c_str(), path);
        return false;
    }
    return true;
}

}  // namespace ledger
