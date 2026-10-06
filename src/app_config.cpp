#include "app_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

// Cardputer（含 ADV）的 microSD 走 SPI，引脚与官方 sdcard 例子一致。
namespace {
constexpr int kSdSck = 40;
constexpr int kSdMiso = 39;
constexpr int kSdMosi = 14;
constexpr int kSdCs = 12;
constexpr uint32_t kSdFreq = 25000000;
constexpr const char* kNvsNamespace = "dswhale";
}  // namespace

bool ConfigStore::begin() {
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    sdReady_ = SD.begin(kSdCs, SPI, kSdFreq);
    if (!sdReady_) {
        sdStatus_ = "no card";
        return false;
    }
    const uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
        sdReady_ = false;
        sdStatus_ = "no card";
        return false;
    }
    if (!SD.exists(dir())) SD.mkdir(dir());
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %.1fGB", type == CARD_SDHC ? "SDHC" : "SD",
             (double)SD.cardSize() / (1024.0 * 1024.0 * 1024.0));
    sdStatus_ = buf;
    return true;
}

bool ConfigStore::loadFromSd(AppConfig& out) {
    if (!sdReady_) return false;
    File f = SD.open(configPath().c_str(), FILE_READ);
    if (!f) return false;
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[cfg] config.json 解析失败: %s\n", err.c_str());
        return false;
    }
    JsonObject wifi = doc["wifi"].as<JsonObject>();
    if (wifi["ssid"].is<const char*>()) out.wifiSsid = wifi["ssid"].as<const char*>();
    if (wifi["pass"].is<const char*>()) out.wifiPass = wifi["pass"].as<const char*>();
    if (doc["api_key"].is<const char*>()) out.apiKey = doc["api_key"].as<const char*>();
    if (doc["refresh_sec"].is<uint32_t>()) out.refreshSec = doc["refresh_sec"].as<uint32_t>();
    if (doc["brightness"].is<int>()) out.brightness = (uint8_t)doc["brightness"].as<int>();
    if (doc["sound"].is<bool>()) out.sound = doc["sound"].as<bool>();
    if (doc["volume"].is<int>()) out.volume = (uint8_t)doc["volume"].as<int>();
    if (doc["tls_verify"].is<bool>()) out.tlsVerify = doc["tls_verify"].as<bool>();
    if (doc["ledger_keep_days"].is<int>()) out.ledgerKeepDays = doc["ledger_keep_days"].as<int>();
    if (doc["bubble_auto_close_sec"].is<int>())
        out.bubbleAutoCloseSec = doc["bubble_auto_close_sec"].as<int>();
    if (doc["show_seconds"].is<bool>()) out.showSeconds = doc["show_seconds"].as<bool>();
    if (out.refreshSec < 15) out.refreshSec = 15;  // 别把 API 打爆
    return true;
}

bool ConfigStore::saveToSd(const AppConfig& cfg) {
    if (!sdReady_) return false;
    if (!SD.exists(dir())) SD.mkdir(dir());
    JsonDocument doc;
    doc["_comment"] = "DeepSeek Whale for M5Cardputer ADV — 字段说明见 docs/CONFIG.md";
    doc["wifi"]["ssid"] = cfg.wifiSsid;
    doc["wifi"]["pass"] = cfg.wifiPass;
    doc["api_key"] = cfg.apiKey;
    doc["refresh_sec"] = cfg.refreshSec;
    doc["brightness"] = cfg.brightness;
    doc["sound"] = cfg.sound;
    doc["volume"] = cfg.volume;
    doc["tls_verify"] = cfg.tlsVerify;
    doc["ledger_keep_days"] = cfg.ledgerKeepDays;
    doc["bubble_auto_close_sec"] = cfg.bubbleAutoCloseSec;
    doc["show_seconds"] = cfg.showSeconds;

    const std::string tmp = configPath() + ".tmp";
    File f = SD.open(tmp.c_str(), FILE_WRITE);
    if (!f) return false;
    const size_t n = serializeJsonPretty(doc, f);
    f.flush();
    f.close();
    if (n == 0) return false;
    SD.remove(configPath().c_str());
    return SD.rename(tmp.c_str(), configPath().c_str());
}

bool ConfigStore::loadFromNvs(AppConfig& out) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) return false;
    const String ssid = prefs.getString("ssid", "");
    const String pass = prefs.getString("pass", "");
    const String key = prefs.getString("key", "");
    prefs.end();
    if (ssid.isEmpty() && key.isEmpty()) return false;
    out.wifiSsid = ssid.c_str();
    out.wifiPass = pass.c_str();
    out.apiKey = key.c_str();
    return true;
}

bool ConfigStore::saveToNvs(const AppConfig& cfg) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) return false;
    prefs.putString("ssid", cfg.wifiSsid.c_str());
    prefs.putString("pass", cfg.wifiPass.c_str());
    prefs.putString("key", cfg.apiKey.c_str());
    prefs.end();
    return true;
}

bool ConfigStore::load(AppConfig& out) {
    if (loadFromSd(out)) return true;
    return loadFromNvs(out);
}

bool ConfigStore::save(const AppConfig& cfg) {
    const bool sd = saveToSd(cfg);
    const bool nvs = saveToNvs(cfg);  // 两条路都写：SD 丢了配置也还在
    return sd || nvs;
}

bool ConfigStore::writeTemplateIfMissing(const AppConfig& cfg) {
    if (!sdReady_) return false;
    if (SD.exists(configPath().c_str())) return false;
    return saveToSd(cfg);
}
