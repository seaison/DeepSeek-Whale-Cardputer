// 配置存储：SD 卡上的 /dswhale/config.json 为主，没插卡时自动退回 NVS
// （ESP32 的 Preferences，存在 flash 里），保证「拔了卡也能跑」。
#pragma once

#include <stdint.h>

#include <string>

struct AppConfig {
    std::string wifiSsid;
    std::string wifiPass;
    std::string apiKey;

    uint32_t refreshSec = 60;         // 余额刷新间隔
    uint8_t brightness = 128;         // 屏幕亮度 0-255
    bool sound = true;                // 音效总开关
    uint8_t volume = 110;             // 0-255
    bool tlsVerify = true;            // 是否校验 api.deepseek.com 的根证书
    int ledgerKeepDays = 90;          // 账本保留天数（与上游 90 天口径一致）
    int bubbleAutoCloseSec = 12;      // 气泡自动关闭秒数，0 = 不自动关
    bool showSeconds = true;          // 时间显示到秒
    std::string language = "en";      // 界面语言："en" / "zh"，见 lang.h
    bool whaleSpin = true;            // 点按鲸鱼时是否播放 360° 旋转动画
};

class ConfigStore {
public:
    // 挂载 SD 卡（插了就挂），并确保 /dswhale 目录存在。
    bool begin();

    // SD config.json 优先 → NVS → 默认值。返回值表示是否读到过用户配置。
    bool load(AppConfig& out);
    bool save(const AppConfig& cfg);

    // 把当前值写成一份带注释性默认值的 config.json（仅在文件不存在时创建）。
    bool writeTemplateIfMissing(const AppConfig& cfg);

    bool sdReady() const { return sdReady_; }
    const std::string& sdStatus() const { return sdStatus_; }

    static const char* dir() { return "/dswhale"; }
    static std::string configPath() { return std::string(dir()) + "/config.json"; }
    static std::string ledgerPath() { return std::string(dir()) + "/ledger.json"; }

private:
    bool loadFromSd(AppConfig& out);
    bool saveToSd(const AppConfig& cfg);
    bool loadFromNvs(AppConfig& out);
    bool saveToNvs(const AppConfig& cfg);

    bool sdReady_ = false;
    std::string sdStatus_ = "not mounted";
};
