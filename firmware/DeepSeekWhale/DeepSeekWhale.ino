/*
 * DeepSeek Whale — M5Stack Cardputer ADV
 * ---------------------------------------------------------------------------
 * 把 DSH Web 插件「DeepSeek 余额小鲸鱼挂件」搬到掌上设备上：
 *   · DeepSeek API 余额（/user/balance，总余额 / 赠金 / 充值余额）
 *   · 今日已用（余额观测记账，按北京时间分日，金额 1e-8 定点累计）
 *   · 峰谷定价时段 + 下一切换点倒计时（含周末与法定节假日规则）
 *   · 点按鲸鱼冒泡：余额 → 今日 → 峰谷 → 随机台词
 *   · 键盘菜单：刷新 / 账本 / 网络 / 设置 / 关于
 *
 * 配置：SD 卡 /dswhale/config.json（没插卡时自动退回 NVS）
 * 账本：SD 卡 /dswhale/ledger.json（原子写入，按密钥指纹分本）
 *
 * 上游: https://github.com/MeteorNOX/DeepSeek-Balance-Whale-Widget (MIT)
 * 本移植: MIT；鲸鱼位图素材沿用上游「as-is」声明，见 NOTICE.md。
 */
#include <M5Cardputer.h>
#include <SD.h>

#include "src/app_config.h"
#include "src/bubbles.h"
#include "src/lang.h"
#include "src/ledger.h"
#include "src/money.h"
#include "src/net_link.h"
#include "src/pricing.h"
#include "src/sound.h"
#include "src/ui.h"

#define APP_VERSION "1.2.1"

// ============================ 全局状态 ============================
static AppConfig g_cfg;
static ConfigStore g_store;
static NetLink g_net;
static ledger::Ledger g_book;
static Ui g_ui;
static ViewModel g_vm;

static Screen g_screen = Screen::Boot;
static int g_menuSel = 0;
static int g_listSel = 0;
static int g_setSel = 0;

static bool g_haveBalance = false;
static BalanceSnapshot g_snap;
static uint32_t g_lastFetchMs = 0;
static bool g_fetching = false;
static uint32_t g_lastLedgerSaveMs = 0;

static uint32_t g_lastRenderMs = 0;
static bool g_forceRender = true;
static int64_t g_lastTick = -1;  // 秒（或分）桶，用来驱动时钟刷新

static bool g_cfgDirty = false;
static uint32_t g_cfgDirtyAt = 0;

static bool g_bubbleOn = false;
static uint32_t g_bubbleUntilMs = 0;
static bubbles::Bubble g_bubble;

// 点按鲸鱼的 360° 旋转动画
static bool g_spinning = false;
static uint32_t g_spinStartMs = 0;
static constexpr uint32_t kWhaleSpinMs = 420;  // 一圈用时

// 按键边沿的最小间隔（防抖：一次物理按下只触发一次动作）
static uint32_t s_lastCharMs = 0;
static uint32_t s_lastEnterMs = 0;
static uint32_t s_lastTabMs = 0;
static uint32_t s_lastBackMs = 0;
static constexpr uint32_t kEdgeGuardMs = 120;
static constexpr uint32_t kCharGuardMs = 40;

static bool g_toastOn = false;
static uint32_t g_toastUntilMs = 0;
static char g_toastText[48] = {};

// 菜单项 = 文案表里的 key（顺序即菜单顺序）
static const lang::Str kMenuStr[] = {
    lang::Str::MenuRefresh, lang::Str::MenuBubble, lang::Str::MenuLedger, lang::Str::MenuNetwork,
    lang::Str::MenuSettings, lang::Str::MenuAbout,  lang::Str::MenuReload, lang::Str::MenuReboot,
};
static constexpr int kMenuCount = sizeof(kMenuStr) / sizeof(kMenuStr[0]);
static constexpr int kMenuReboot = kMenuCount - 1;
static constexpr int kMenuReload = kMenuCount - 2;

// ============================ 小工具 ============================
static void toast(const char* text) {
    snprintf(g_toastText, sizeof(g_toastText), "%s", text);
    g_toastOn = true;
    g_toastUntilMs = millis() + 1800;
    g_forceRender = true;
}

static void bootStage(const char* stage, const char* detail) {
    g_ui.drawBoot(stage, detail);
}

static void fillVm() {
    g_vm.wifiConfigured = !g_cfg.wifiSsid.empty();
    g_vm.wifiOnline = g_net.online();
    g_vm.netText = g_net.stateText();
    g_vm.rssi = g_net.rssi();
    g_vm.ip = g_net.ip();

    g_vm.timeSynced = g_net.timeSynced();
    g_vm.nowUtc = g_vm.timeSynced ? g_net.nowUtc() : 0;

    g_vm.haveBalance = g_haveBalance;
    g_vm.fetching = g_fetching;
    g_vm.total = g_snap.total;
    g_vm.granted = g_snap.granted;
    g_vm.toppedUp = g_snap.toppedUp;
    snprintf(g_vm.currency, sizeof(g_vm.currency), "%s", g_snap.currency);
    g_vm.balanceAt = g_snap.at;
    g_vm.latencyMs = g_snap.latencyMs;

    g_vm.today = g_vm.nowUtc ? g_book.today(g_vm.nowUtc) : ledger::Summary();
    g_vm.bookCount = (int)g_book.bookCount();
    g_vm.dayCount = g_book.dayCount();
    g_vm.scope = g_book.activeScope().substr(0, g_book.activeScope().size() >= 4
                                                   ? g_book.activeScope().size() - 4
                                                   : 0);
    g_vm.firstDay = g_book.firstDay();

    g_vm.peak = g_vm.nowUtc ? pricing::isPeakTime(g_vm.nowUtc) : false;
    g_vm.nextChangeAt = g_vm.nowUtc ? pricing::nextPeakChangeAt(g_vm.nowUtc) : 0;

    g_vm.showSeconds = g_cfg.showSeconds;
    g_vm.sdReady = g_store.sdReady();
    g_vm.sdStatus = g_store.sdStatus();
    g_vm.version = APP_VERSION;
    g_vm.buildDate = __DATE__;

    g_vm.bubbleCountdown = 0;
    if (g_bubbleOn && g_cfg.bubbleAutoCloseSec > 0) {
        const int64_t left = (int64_t)g_bubbleUntilMs - (int64_t)millis();
        g_vm.bubbleCountdown = left > 0 ? (int)(left / 1000) + 1 : 0;
    }
}

// ============================ 配置 / 账本 ============================
// 切换界面语言：立即生效并写进配置（延迟落盘由 loop 里的 cfgDirty 负责）
static void applyLanguage(Lang l) {
    lang::set(l);
    g_cfg.language = lang::code();
    g_forceRender = true;
}

static void toggleLanguage() {
    applyLanguage(lang::current() == Lang::Zh ? Lang::En : Lang::Zh);
}

static void applyConfig() {
    M5Cardputer.Display.setBrightness(g_cfg.brightness);
    sound::begin(g_cfg.sound, g_cfg.volume);
    if (!g_cfg.apiKey.empty()) {
        g_book.begin(ledger::scopeFromKey(g_cfg.apiKey), "CNY");
    }
}

static void saveLedger(bool force) {
    if (!g_book.dirty()) return;
    if (!g_store.sdReady()) return;
    const uint32_t now = millis();
    if (!force && now - g_lastLedgerSaveMs < 30000) return;
    if (g_book.saveToFile(ConfigStore::ledgerPath().c_str())) {
        g_lastLedgerSaveMs = now;
        g_book.clearDirty();
    }
}

static void reloadConfig() {
    AppConfig fresh;
    const bool got = g_store.load(fresh);
    g_cfg = fresh;
    applyConfig();
    g_net.setCredentials(g_cfg.wifiSsid, g_cfg.wifiPass);
    g_net.begin(g_cfg.wifiSsid, g_cfg.wifiPass);
    toast(lang::t(got ? lang::Str::ToastConfigReloaded : lang::Str::ToastNoConfig));
}

// ============================ 网络 / 数据 ============================
static bool waitWifi(uint32_t timeoutMs) {
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        M5Cardputer.update();
        g_net.loop();
        if (g_net.online()) return true;
        char detail[48];
        snprintf(detail, sizeof(detail), "%s  %lus", g_cfg.wifiSsid.c_str(),
                 (unsigned long)((millis() - start) / 1000));
        bootStage(lang::t(lang::Str::BootWifiConnecting), detail);
        delay(50);
    }
    return g_net.online();
}

static bool waitTime(uint32_t timeoutMs) {
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        M5Cardputer.update();
        g_net.loop();
        if (g_net.timeSynced()) return true;
        bootStage(lang::t(lang::Str::BootNtp), lang::t(lang::Str::BootBeijing));
        delay(50);
    }
    return g_net.timeSynced();
}

static bool doFetch(bool announce) {
    if (!g_net.online()) {
        g_vm.lastError = lang::t(g_cfg.wifiSsid.empty() ? lang::Str::ErrNoWifiConfig
                                                       : lang::Str::ErrWifiOffline);
        if (announce) sound::error();
        return false;
    }
    g_fetching = true;
    g_forceRender = true;
    fillVm();
    g_ui.drawMain(g_vm);

    BalanceSnapshot snap;
    const bool ok = g_net.fetchBalance(g_cfg.apiKey, g_cfg.tlsVerify, snap);
    g_fetching = false;
    g_snap = snap;
    g_lastFetchMs = millis();

    if (ok) {
        g_haveBalance = true;
        g_vm.lastError.clear();
        if (snap.at > 0) {
            g_book.begin(ledger::scopeFromKey(g_cfg.apiKey), snap.currency);
            g_book.observe(snap.at, snap.total, snap.currency);
            g_book.prune(g_cfg.ledgerKeepDays, snap.at);
            saveLedger(false);
        }
        if (announce) sound::ok();
    } else {
        if (g_haveBalance) {
            // 瞬时网络抖动不清空已有余额（与上游一致：沿用最近一次的值）
            g_vm.lastError = snap.message;
        } else {
            g_vm.lastError = snap.message;
        }
        if (announce) sound::error();
    }
    g_forceRender = true;
    return ok;
}

// ============================ 气泡 ============================
static void showBubble(bool advance) {
    if (advance) {
        g_bubble = bubbles::next(g_vm);
    } else {
        g_bubble = bubbles::random_(g_vm);
    }
    g_bubbleOn = true;
    g_bubbleUntilMs = g_cfg.bubbleAutoCloseSec > 0
                          ? millis() + (uint32_t)g_cfg.bubbleAutoCloseSec * 1000u
                          : 0;
    sound::whaleClick();  // 点鲸鱼的声音和普通按键分开
    if (g_cfg.whaleSpin) {
        g_spinning = true;
        g_spinStartMs = millis();
    }
    g_forceRender = true;
}

static void hideBubble() {
    g_bubbleOn = false;
    g_forceRender = true;
}

// ============================ 设置项 ============================
enum SettingId {
    kSetBrightness = 0,
    kSetSound,
    kSetVolume,
    kSetRefresh,
    kSetTls,
    kSetBubbleClose,
    kSetSeconds,
    kSetLanguage,
    kSetWhaleSpin,
    kSetSave,
    kSetCount,
};

static void settingLabels(std::vector<std::string>& labels, std::vector<std::string>& values) {
    char buf[24];
    labels = {lang::t(lang::Str::SetBrightness), lang::t(lang::Str::SetSound),
              lang::t(lang::Str::SetVolume),     lang::t(lang::Str::SetRefresh),
              lang::t(lang::Str::SetTls),        lang::t(lang::Str::SetBubbleClose),
              lang::t(lang::Str::SetSeconds),    lang::t(lang::Str::SetLanguage),
              lang::t(lang::Str::SetWhaleSpin),  lang::t(lang::Str::SetSave)};
    const char* on = lang::t(lang::Str::ValOn);
    const char* off = lang::t(lang::Str::ValOff);
    values.clear();
    snprintf(buf, sizeof(buf), "%u", (unsigned)g_cfg.brightness);
    values.push_back(buf);
    values.push_back(g_cfg.sound ? on : off);
    snprintf(buf, sizeof(buf), "%u", (unsigned)g_cfg.volume);
    values.push_back(buf);
    snprintf(buf, sizeof(buf), "%us", (unsigned)g_cfg.refreshSec);
    values.push_back(buf);
    values.push_back(g_cfg.tlsVerify ? on : off);
    snprintf(buf, sizeof(buf), "%ds", g_cfg.bubbleAutoCloseSec);
    values.push_back(buf);
    values.push_back(g_cfg.showSeconds ? on : off);
    values.push_back(lang::t(lang::Str::ValLangName));  // English / 中文
    values.push_back(g_cfg.whaleSpin ? on : off);
    values.push_back("-");
}

static void markCfgDirty() {
    g_cfgDirty = true;
    g_cfgDirtyAt = millis();
}

static void adjustSetting(int dir) {
    switch (g_setSel) {
        case kSetBrightness:
            g_cfg.brightness = (uint8_t)constrain((int)g_cfg.brightness + dir * 16, 16, 255);
            M5Cardputer.Display.setBrightness(g_cfg.brightness);
            break;
        case kSetSound:
            g_cfg.sound = !g_cfg.sound;
            sound::setEnabled(g_cfg.sound);
            break;
        case kSetVolume:
            g_cfg.volume = (uint8_t)constrain((int)g_cfg.volume + dir * 16, 0, 255);
            sound::setVolume(g_cfg.volume);
            sound::keyPress();
            break;
        case kSetRefresh:
            g_cfg.refreshSec = (uint32_t)constrain((int)g_cfg.refreshSec + dir * 15, 15, 900);
            break;
        case kSetTls:
            g_cfg.tlsVerify = !g_cfg.tlsVerify;
            break;
        case kSetBubbleClose:
            g_cfg.bubbleAutoCloseSec = constrain(g_cfg.bubbleAutoCloseSec + dir * 5, 0, 120);
            break;
        case kSetSeconds:
            g_cfg.showSeconds = !g_cfg.showSeconds;
            break;
        case kSetLanguage:
            toggleLanguage();
            break;
        case kSetWhaleSpin:
            g_cfg.whaleSpin = !g_cfg.whaleSpin;
            break;
        case kSetSave: {
            const bool ok = g_store.save(g_cfg);
            toast(lang::t(ok ? lang::Str::ToastConfigSaved : lang::Str::ToastSaveFailed));
            g_cfgDirty = false;
            sound::ok();
            break;
        }
        default:
            break;
    }
    if (g_setSel != kSetSave) markCfgDirty();
    g_forceRender = true;
}

// ============================ 键盘 ============================
static std::string s_prevWord;
static bool s_prevTab = false, s_prevEnter = false, s_prevDel = false;
static int s_navDir = 0;
static uint32_t s_navNextMs = 0;

static bool charIsNew(char c, const std::string& cur) {
    size_t inCur = 0, inPrev = 0;
    for (char x : cur)
        if (x == c) ++inCur;
    for (char x : s_prevWord)
        if (x == c) ++inPrev;
    return inCur > inPrev;
}

static int navDirOf(const std::string& cur) {
    for (char c : cur) {
        switch (c) {
            case ';': case 'w': case 'W': return 1;  // 上
            case '.': case 's': case 'S': return 2;  // 下
            case ',': case 'a': case 'A': return 3;  // 左
            case '/': case 'd': case 'D': return 4;  // 右
            default: break;
        }
    }
    return 0;
}

static void goBack() {
    // 气泡是叠加层：显示中先收气泡，再谈换屏
    if (g_screen == Screen::Main && g_bubbleOn) {
        hideBubble();
        sound::keyRelease();
        return;
    }
    switch (g_screen) {
        case Screen::Main: g_screen = Screen::Menu; break;
        case Screen::Menu: g_screen = Screen::Main; break;
        default: g_screen = Screen::Menu; break;
    }
    g_forceRender = true;
    sound::keyRelease();
}

static void activateMenu();

static void handleNav(int dir) {
    switch (g_screen) {
        case Screen::Menu:
            if (dir == 1) g_menuSel = (g_menuSel + kMenuCount - 1) % kMenuCount;
            if (dir == 2) g_menuSel = (g_menuSel + 1) % kMenuCount;
            break;
        case Screen::Ledger:
            if (dir == 1 && g_listSel > 0) --g_listSel;
            if (dir == 2) ++g_listSel;
            if (dir == 3 || dir == 4) --g_listSel;
            break;
        case Screen::Net:
            if (dir == 1 && g_listSel > 0) --g_listSel;
            if (dir == 2) ++g_listSel;
            break;
        case Screen::Settings:
            if (dir == 1) g_setSel = (g_setSel + kSetCount - 1) % kSetCount;
            if (dir == 2) g_setSel = (g_setSel + 1) % kSetCount;
            if (dir == 3) adjustSetting(-1);
            if (dir == 4) adjustSetting(+1);
            break;
        default:
            break;
    }
    g_forceRender = true;
}

static void handleChar(char c) {
    if (c == '`') {
        goBack();
        return;
    }
    switch (g_screen) {
        case Screen::Main:
            if (c == 'r' || c == 'R') {
                doFetch(true);
            } else if (c == 'l' || c == 'L') {
                toggleLanguage();  // 中英一键切换
                markCfgDirty();
            } else if (c == ' ') {
                showBubble(true);
            }
            break;
        case Screen::Menu:
            if (c == 'r' || c == 'R') doFetch(true);
            break;
        case Screen::Settings:
            if (c == 'r' || c == 'R') doFetch(true);
            break;
        default:
            break;
    }
}

static void handleEnter() {
    switch (g_screen) {
        case Screen::Main:
            showBubble(true);  // 内部放 whaleClick
            break;
        case Screen::Menu:
            sound::keyPress();
            activateMenu();
            break;
        case Screen::Settings:
            sound::keyPress();
            if (g_setSel == kSetSave) {
                adjustSetting(0);
            } else if (g_setSel == kSetSound || g_setSel == kSetTls || g_setSel == kSetSeconds ||
                       g_setSel == kSetLanguage || g_setSel == kSetWhaleSpin) {
                adjustSetting(0);
            }
            break;
        case Screen::Ledger:
        case Screen::Net:
        case Screen::About:
            sound::keyRelease();
            g_screen = Screen::Menu;
            g_forceRender = true;
            break;
        default:
            break;
    }
}

static void pollKeys() {
    Keyboard_Class& kb = M5Cardputer.Keyboard;
    std::string cur;
    bool tab = false, enter = false, del = false;

    if (kb.isPressed()) {
        Keyboard_Class::KeysState st = kb.keysState();
        cur.assign(st.word.begin(), st.word.end());
        tab = st.tab;
        enter = st.enter;
        del = st.del;
    }
    kb.isChange();  // 不再依赖它做判据，只是把库内部状态消费掉

    const uint32_t now = millis();

    // 字符键：与上一帧求差集，**每帧都判**。
    // 不能只在 isChange() 里判 —— 同时按下/松开时按键数量不变，isChange() 不触发，会漏事件。
    for (char c : cur) {
        if (!charIsNew(c, cur)) continue;
        if (now - s_lastCharMs < kCharGuardMs) continue;
        s_lastCharMs = now;
        if (c != ' ') sound::keyPress();  // 空格 = 点鲸鱼，声音交给 showBubble
        handleChar(c);
    }

    // 特殊键：上升沿 + 最小间隔 ⇒ 一次物理按下只做一次动作（防抖）
    if (enter && !s_prevEnter && now - s_lastEnterMs >= kEdgeGuardMs) {
        s_lastEnterMs = now;
        handleEnter();
    }
    if (tab && !s_prevTab && now - s_lastTabMs >= kEdgeGuardMs) {
        s_lastTabMs = now;
        sound::keyPress();
        g_screen = (g_screen == Screen::Menu) ? Screen::Main : Screen::Menu;
        g_forceRender = true;
    }
    if (del && !s_prevDel && now - s_lastBackMs >= kEdgeGuardMs) {
        s_lastBackMs = now;
        goBack();
    }

    s_prevTab = tab;
    s_prevEnter = enter;
    s_prevDel = del;
    s_prevWord = cur;

    // 长按方向键：首字触发后 400ms 开始每 110ms 重复一次（只用于导航，不影响其他键）
    const int dir = navDirOf(cur);
    if (dir == 0) {
        s_navDir = 0;
    } else if (dir != s_navDir) {
        s_navDir = dir;
        s_navNextMs = now + 400;
        handleNav(dir);
    } else if (now >= s_navNextMs) {
        s_navNextMs = now + 110;
        handleNav(dir);
    }
}

static void activateMenu() {
    switch (g_menuSel) {
        case 0: doFetch(true); break;
        case 1: showBubble(true); break;
        case 2: g_screen = Screen::Ledger; g_listSel = 0; break;
        case 3: g_screen = Screen::Net; g_listSel = 0; break;
        case 4: g_screen = Screen::Settings; g_setSel = 0; break;
        case 5: g_screen = Screen::About; break;  // About
        case kMenuReload: reloadConfig(); break;
        case kMenuReboot:
            toast(lang::t(lang::Str::ToastRebooting));
            fillVm();
            g_ui.setToast(g_toastText);
            g_ui.setWhaleAngle(0.0f);
            g_ui.drawMain(g_vm);
            delay(400);
            ESP.restart();
            break;
        default: break;
    }
    g_forceRender = true;
}

// ============================ 渲染 ============================
static void buildLedgerRows(std::vector<std::pair<std::string, std::string>>& rows) {
    rows.clear();
    const auto days = g_book.recentDays(60);
    for (const auto& d : days) {
        char amount[24];
        money::format(d.second, amount, sizeof(amount), 2);
        rows.emplace_back(d.first, std::string(amount));
    }
    if (rows.empty()) rows.emplace_back(lang::t(lang::Str::LedgerEmpty), "-");
}

static void buildNetRows(std::vector<std::pair<std::string, std::string>>& rows) {
    rows.clear();
    char buf[40];
    rows.emplace_back(lang::t(lang::Str::NetState), g_net.stateText());
    rows.emplace_back(lang::t(lang::Str::NetSsid), g_cfg.wifiSsid.empty() ? "-" : g_cfg.wifiSsid);
    rows.emplace_back(lang::t(lang::Str::NetIp), g_vm.ip);
    snprintf(buf, sizeof(buf), "%d dBm", (int)g_vm.rssi);
    rows.emplace_back(lang::t(lang::Str::NetRssi), buf);
    rows.emplace_back(lang::t(lang::Str::NetTimeSync),
                     lang::t(g_vm.timeSynced ? lang::Str::ValOk : lang::Str::ValPending));
    if (g_vm.timeSynced) {
        const pricing::BeijingTime bt = pricing::beijing(g_vm.nowUtc);
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", bt.year, bt.month, bt.day,
                 bt.hour, bt.minute, bt.second);
        rows.emplace_back(lang::t(lang::Str::NetBeijing), buf);
    }
    rows.emplace_back(lang::t(lang::Str::NetApiKey),
                     lang::t(g_cfg.apiKey.empty() ? lang::Str::ValMissing : lang::Str::ValSet));
    snprintf(buf, sizeof(buf), "HTTP %d", g_snap.httpCode);
    rows.emplace_back(lang::t(lang::Str::NetLastFetch), buf);
    snprintf(buf, sizeof(buf), "%ums", (unsigned)g_snap.latencyMs);
    rows.emplace_back(lang::t(lang::Str::NetLatency), buf);
    rows.emplace_back(lang::t(lang::Str::NetCurrency), g_snap.currency);
    rows.emplace_back(lang::t(lang::Str::NetAvailable),
                     lang::t(g_snap.available ? lang::Str::ValYes : lang::Str::ValNo));
    char m[24];
    money::format(g_snap.granted, m, sizeof(m), 2);
    rows.emplace_back(lang::t(lang::Str::NetGranted), m);
    money::format(g_snap.toppedUp, m, sizeof(m), 2);
    rows.emplace_back(lang::t(lang::Str::NetToppedUp), m);
    rows.emplace_back(lang::t(lang::Str::NetTls),
                     lang::t(g_cfg.tlsVerify ? lang::Str::ValOn : lang::Str::ValOff));
    rows.emplace_back(lang::t(lang::Str::NetSd),
                     g_vm.sdReady ? g_vm.sdStatus : lang::t(lang::Str::ValNotPresent));
    rows.emplace_back(lang::t(lang::Str::NetAccount), g_vm.scope);
    rows.emplace_back(lang::t(lang::Str::NetBooks), std::to_string(g_vm.bookCount));
    rows.emplace_back(lang::t(lang::Str::NetMessage), g_snap.message);
}

static void render() {
    const uint32_t now = millis();

    // 时钟每秒（show_seconds 关掉时每分钟）跳一次 → 只在这种时候才重绘。
    // 以前是固定 5fps 全屏重绘：内容没变也推屏，既费电又更容易看出撕裂。
    const int64_t tick = g_net.timeSynced() ? g_net.nowUtc() : (int64_t)(now / 1000);
    const int64_t bucket = g_cfg.showSeconds ? tick : tick / 60;
    if (bucket != g_lastTick) {
        g_lastTick = bucket;
        g_forceRender = true;
    }

    // 旋转动画：420ms 转满 360°，动画期间把刷新间隔压到 30ms
    float angle = 0.0f;
    if (g_spinning) {
        const uint32_t elapsed = now - g_spinStartMs;
        if (elapsed >= kWhaleSpinMs) {
            g_spinning = false;  // 结束时角度正好回到 0，不会跳
        } else {
            angle = 360.0f * (float)elapsed / (float)kWhaleSpinMs;
        }
    }
    if (!g_forceRender && now - g_lastRenderMs < (g_spinning ? 30u : 200u)) return;
    g_lastRenderMs = now;
    g_forceRender = false;
    g_ui.setWhaleAngle(angle);

    fillVm();
    if (g_toastOn && now > g_toastUntilMs) g_toastOn = false;
    g_ui.setToast(g_toastOn ? g_toastText : nullptr);  // 各屏推屏前统一画，避免闪
    if (g_bubbleOn && g_bubbleUntilMs > 0 && now > g_bubbleUntilMs) g_bubbleOn = false;

    switch (g_screen) {
        case Screen::Menu: {
            std::vector<std::string> items;
            for (int i = 0; i < kMenuCount; ++i) items.emplace_back(lang::t(kMenuStr[i]));
            char sub[48];
            snprintf(sub, sizeof(sub), lang::t(lang::Str::MenuAcctFmt), g_vm.scope.c_str());
            g_ui.drawMenu(g_vm, items, g_menuSel, sub);
            break;
        }
        case Screen::Ledger: {
            std::vector<std::pair<std::string, std::string>> rows;
            buildLedgerRows(rows);
            char foot[64];
            snprintf(foot, sizeof(foot), lang::t(lang::Str::LedgerFootFmt), (unsigned)g_vm.dayCount,
                     (unsigned)g_vm.bookCount);
            g_ui.drawList(g_vm, lang::t(lang::Str::LedgerTitle), rows, g_listSel, foot);
            break;
        }
        case Screen::Net: {
            std::vector<std::pair<std::string, std::string>> rows;
            buildNetRows(rows);
            g_ui.drawList(g_vm, lang::t(lang::Str::NetTitle), rows, g_listSel, nullptr);
            break;
        }
        case Screen::Settings: {
            std::vector<std::string> labels, values;
            settingLabels(labels, values);
            g_ui.drawSettings(g_vm, labels, values, g_setSel);
            break;
        }
        case Screen::About:
            g_ui.drawAbout(g_vm);
            break;
        case Screen::Main:
        default:
            if (g_bubbleOn) {
                g_ui.drawBubble(g_vm, g_bubble.title.c_str(), g_bubble.lines);
            } else {
                g_ui.drawMain(g_vm);
            }
            break;
    }
}

// ============================ setup / loop ============================
void setup() {
    auto m5cfg = M5.config();
    M5Cardputer.begin(m5cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(128);
    Serial.begin(115200);
    Serial.printf("\n[whale] DeepSeek Whale %s for Cardputer ADV\n", APP_VERSION);

    // 先把配置读进来（此时还没画任何东西），语言才能在第一屏就正确。
    // SD 挂载约 0.3s，这期间是黑屏，属于正常。
    g_store.begin();
    Serial.printf("[whale] SD: %s\n", g_store.sdStatus().c_str());
    {
        AppConfig loaded;
        const bool hasCfg = g_store.load(loaded);
        if (hasCfg) g_cfg = loaded;
    }
    lang::set(lang::fromCode(g_cfg.language.c_str()));

    if (!g_ui.begin()) {
        Serial.println("[whale] 离屏缓冲创建失败");
    }
    bootStage(lang::t(lang::Str::BootBooting), lang::t(lang::Str::BootBoard));

    g_store.writeTemplateIfMissing(g_cfg);
    applyConfig();

    if (g_store.sdReady()) {
        if (g_book.loadFromFile(ConfigStore::ledgerPath().c_str())) {
            Serial.printf("[whale] 账本已载入: %u 天 %u 本\n", (unsigned)g_book.dayCount(),
                          (unsigned)g_book.bookCount());
        }
    }
    g_book.begin(ledger::scopeFromKey(g_cfg.apiKey), "CNY");

    if (!g_cfg.wifiSsid.empty()) {
        bootStage(lang::t(lang::Str::BootWifi), g_cfg.wifiSsid.c_str());
        g_net.begin(g_cfg.wifiSsid, g_cfg.wifiPass);
        if (waitWifi(15000)) {
            waitTime(12000);
            bootStage(lang::t(lang::Str::BootBalance), lang::t(lang::Str::BootBalanceUrl));
            doFetch(false);
        } else {
            Serial.println("[whale] WiFi 连接超时，进入主界面后自动重试");
        }
    } else {
        bootStage(lang::t(lang::Str::BootNoWifiConfig), lang::t(lang::Str::BootPutConfig));
        Serial.println("[whale] 缺少 WiFi 配置，见 docs/CONFIG.md");
        delay(1500);
    }

    g_screen = Screen::Main;
    g_forceRender = true;
    render();
}

void loop() {
    M5Cardputer.update();
    g_net.loop();
    sound::loop();

    pollKeys();

    // 定时刷新余额
    const uint32_t now = millis();
    if (g_net.online() && !g_fetching && now - g_lastFetchMs >= g_cfg.refreshSec * 1000u) {
        doFetch(false);
    }

    // 配置改动后延迟落盘（避免每次按键都写卡）
    if (g_cfgDirty && now - g_cfgDirtyAt > 2500) {
        g_cfgDirty = false;
        const bool ok = g_store.save(g_cfg);
        toast(lang::t(ok ? lang::Str::ToastConfigSaved : lang::Str::ToastSaveFailed));
    }

    saveLedger(false);
    render();
    delay(5);
}
