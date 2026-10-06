#include "net_link.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "money.h"
#include "root_ca.h"

namespace {
constexpr const char* kBalanceUrl = "https://api.deepseek.com/user/balance";
constexpr uint32_t kHttpTimeoutMs = 12000;
// 2020-09-13 之后的时间戳才算 NTP 同步成功
constexpr int64_t kSaneEpoch = 1600000000LL;
}  // namespace

void NetLink::begin(const std::string& ssid, const std::string& pass) {
    setCredentials(ssid, pass);
    if (ssid_.empty()) {
        state_ = NetState::Idle;
        return;
    }
    startConnect();
}

void NetLink::setCredentials(const std::string& ssid, const std::string& pass) {
    ssid_ = ssid;
    pass_ = pass;
}

void NetLink::startConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // 关掉省电，长连接/HTTPS 更稳
    WiFi.begin(ssid_.c_str(), pass_.c_str());
    state_ = NetState::Connecting;
    lastAttemptMs_ = millis();
}

void NetLink::loop() {
    if (ssid_.empty()) {
        state_ = NetState::Idle;
        return;
    }
    const uint32_t now = millis();
    const wl_status_t st = WiFi.status();

    if (st == WL_CONNECTED) {
        if (state_ != NetState::Online) {
            state_ = NetState::Online;
            backoffMs_ = 3000;
            Serial.printf("[net] WiFi 已连接 %s RSSI=%d\n", WiFi.localIP().toString().c_str(),
                          (int)WiFi.RSSI());
        }
        if (!timeRequested_) requestTimeSync();
        if (!clockSynced_) {
            const int64_t t = nowUtc();
            if (t > 0) {
                clockSynced_ = true;
                Serial.printf("[net] NTP 对时完成 t=%lld\n", (long long)t);
            }
        }
        return;
    }

    state_ = NetState::Connecting;
    if (now - lastAttemptMs_ >= backoffMs_) {
        Serial.println("[net] WiFi 重连…");
        WiFi.disconnect();
        WiFi.begin(ssid_.c_str(), pass_.c_str());
        lastAttemptMs_ = now;
        backoffMs_ = backoffMs_ >= 30000 ? 30000 : backoffMs_ * 2;  // 3s→30s 退避
    }
}

bool NetLink::online() const {
    return WiFi.status() == WL_CONNECTED;
}

const char* NetLink::stateText() const {
    switch (state_) {
        case NetState::Online: return "online";
        case NetState::Connecting: return "connecting";
        case NetState::Offline: return "offline";
        default: return ssid_.empty() ? "no ssid" : "idle";
    }
}

int32_t NetLink::rssi() const {
    return online() ? WiFi.RSSI() : 0;
}

std::string NetLink::ip() const {
    return online() ? std::string(WiFi.localIP().toString().c_str()) : std::string("-");
}

void NetLink::requestTimeSync() {
    configTime(0, 0, "ntp.aliyun.com", "ntp.tencent.com", "pool.ntp.org");
    timeRequested_ = true;
}

bool NetLink::timeSynced() const {
    return nowUtc() > 0;
}

int64_t NetLink::nowUtc() const {
    const time_t t = time(nullptr);
    return ((int64_t)t > kSaneEpoch) ? (int64_t)t : 0;
}

bool NetLink::fetchBalance(const std::string& apiKey, bool tlsVerify, BalanceSnapshot& out) {
    out = BalanceSnapshot();
    if (!online()) {
        snprintf(out.message, sizeof(out.message), "WiFi offline");
        return false;
    }
    if (apiKey.empty()) {
        snprintf(out.message, sizeof(out.message), "no api key");
        return false;
    }
    // 证书校验需要正确时间，没对时成功就退回不校验，避免一直握手失败。
    const bool verify = tlsVerify && timeSynced();

    WiFiClientSecure client;
    client.setHandshakeTimeout(10);
    if (verify) {
        // kRootCaPem 里是**多张**根证书拼成的一段 PEM（国内 DigiCert G2 / 海外 Amazon Root CA 1），
        // mbedtls 一次解析全部，见 src/root_ca.h 与 tools/certs/README.md。
        client.setCACert(kRootCaPem);
    } else {
        client.setInsecure();
    }

    HTTPClient http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    http.setReuse(false);
    if (!http.begin(client, kBalanceUrl)) {
        snprintf(out.message, sizeof(out.message), "http begin failed");
        return false;
    }
    http.addHeader("Authorization", (std::string("Bearer ") + apiKey).c_str());
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "DeepSeekWhale-Cardputer/1.0");

    const uint32_t t0 = millis();
    const int code = http.GET();
    out.latencyMs = millis() - t0;
    out.httpCode = code;
    if (code != HTTP_CODE_OK) {
        if (code == 401) {
            snprintf(out.message, sizeof(out.message), "401 bad api key");
        } else if (code == 402) {
            snprintf(out.message, sizeof(out.message), "402 payment required");
        } else if (code < 0) {
            snprintf(out.message, sizeof(out.message), "net err %d%s", code,
                     verify ? "" : " (no tls verify)");
        } else {
            snprintf(out.message, sizeof(out.message), "HTTP %d", code);
        }
        http.end();
        return false;
    }

    // 只解析需要的字段，省内存。
    JsonDocument filter;
    filter["is_available"] = true;
    filter["balance_infos"][0]["currency"] = true;
    filter["balance_infos"][0]["total_balance"] = true;
    filter["balance_infos"][0]["granted_balance"] = true;
    filter["balance_infos"][0]["topped_up_balance"] = true;

    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        snprintf(out.message, sizeof(out.message), "json: %s", err.c_str());
        return false;
    }

    out.available = doc["is_available"].as<bool>();
    JsonArray infos = doc["balance_infos"].as<JsonArray>();
    if (infos.size() == 0) {
        snprintf(out.message, sizeof(out.message), "no balance_infos");
        return false;
    }
    // 优先人民币钱包，没有就取第一条（上游取 [0]，这里对多币种更友好一点）。
    JsonObject chosen = infos[0].as<JsonObject>();
    for (JsonObject info : infos) {
        const char* cur = info["currency"].as<const char*>();
        if (cur && strcmp(cur, "CNY") == 0) {
            chosen = info;
            break;
        }
    }
    const char* cur = chosen["currency"].as<const char*>();
    if (cur) snprintf(out.currency, sizeof(out.currency), "%s", cur);
    // 注意：这三个字段在 API 里是**字符串**（"110.00"），要按字符串解析。
    money::parse(chosen["total_balance"].as<const char*>(), out.total);
    money::parse(chosen["granted_balance"].as<const char*>(), out.granted);
    money::parse(chosen["topped_up_balance"].as<const char*>(), out.toppedUp);

    out.ok = true;
    out.at = nowUtc();
    snprintf(out.message, sizeof(out.message), "OK %ums", (unsigned)out.latencyMs);
    return true;
}
