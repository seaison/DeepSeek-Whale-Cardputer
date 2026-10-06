// 网络侧：WiFi 连接状态机 + NTP 对时 + DeepSeek 余额接口。
//
// 余额接口：GET https://api.deepseek.com/user/balance
//   Authorization: Bearer <DEEPSEEK_API_KEY>
//   返回 {"is_available":true,"balance_infos":[{"currency":"CNY",
//        "total_balance":"110.00","granted_balance":"10.00","topped_up_balance":"100.00"}]}
#pragma once

#include <stdint.h>

#include <string>

enum class NetState : uint8_t { Idle, Connecting, Online, Offline };

struct BalanceSnapshot {
    bool ok = false;          // 本次请求是否成功拿到可用余额
    int httpCode = 0;
    bool available = false;   // is_available（账户余额是否够用）
    char currency[8] = "CNY";
    int64_t total = 0;        // 总余额（定点，记账基准）
    int64_t granted = 0;      // 赠金
    int64_t toppedUp = 0;     // 充值余额
    int64_t at = 0;           // 观测时刻（UTC epoch 秒）
    uint32_t latencyMs = 0;
    char message[96] = {};    // 成功 = "OK"，失败 = 原因（直接上屏）
};

class NetLink {
public:
    void begin(const std::string& ssid, const std::string& pass);
    void setCredentials(const std::string& ssid, const std::string& pass);

    void loop();  // 非阻塞：推进连接 / 断线重连（带退避）

    NetState state() const { return state_; }
    bool online() const;
    const char* stateText() const;
    int32_t rssi() const;
    std::string ip() const;

    bool timeSynced() const;
    int64_t nowUtc() const;  // 未同步返回 0
    void requestTimeSync();

    // 阻塞直到响应或超时；返回是否拿到有效余额。
    bool fetchBalance(const std::string& apiKey, bool tlsVerify, BalanceSnapshot& out);

private:
    void startConnect();

    std::string ssid_;
    std::string pass_;
    NetState state_ = NetState::Idle;
    uint32_t lastAttemptMs_ = 0;
    uint32_t backoffMs_ = 3000;
    bool timeRequested_ = false;
    bool clockSynced_ = false;
};
