#include "sound.h"

#include <M5Cardputer.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <esp32-hal-psram.h>

namespace sound {
namespace {

struct Note {
    float freq;
    uint16_t ms;
};

// 合成音序列（短促、不刺耳；音量跟设置走）
const Note kOk[] = {{1046.5f, 55}, {1568.0f, 90}};
const Note kDone[] = {{880.0f, 70}, {1174.7f, 70}, {1568.0f, 130}};
const Note kErr[] = {{392.0f, 110}, {261.6f, 160}};

bool s_enabled = true;
uint8_t s_volume = 110;

const Note* s_seq = nullptr;
size_t s_seqLen = 0;
size_t s_seqIdx = 0;
uint32_t s_seqNextMs = 0;
bool s_seqActive = false;

uint8_t* s_wavBuf = nullptr;
size_t s_wavCap = 0;

void* allocPreferPsram(size_t n) {
    void* p = nullptr;
    if (psramFound()) p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!p) p = malloc(n);
    return p;
}

void startSequence(const Note* seq, size_t len) {
    if (!s_enabled) return;
    s_seq = seq;
    s_seqLen = len;
    s_seqIdx = 0;
    s_seqActive = true;
    s_seqNextMs = 0;  // loop() 里立刻触发第一个音
}

}  // namespace

void begin(bool enabled, uint8_t volume) {
    s_enabled = enabled;
    s_volume = volume;
    M5.Speaker.setVolume(s_volume);
}

void loop() {
    if (!s_seqActive) return;
    const uint32_t now = millis();
    if (now < s_seqNextMs) return;
    if (s_seqIdx >= s_seqLen) {
        s_seqActive = false;
        return;
    }
    const Note& n = s_seq[s_seqIdx++];
    M5.Speaker.tone(n.freq, n.ms, -1, true);
    s_seqNextMs = now + n.ms + 10;
}

void setEnabled(bool on) {
    s_enabled = on;
    if (!on) {
        s_seqActive = false;
        M5.Speaker.stop();
    }
}

bool enabled() {
    return s_enabled;
}

void setVolume(uint8_t vol) {
    s_volume = vol;
    M5.Speaker.setVolume(vol);
}

uint8_t volume() {
    return s_volume;
}

void keyPress() {
    if (!s_enabled) return;
    M5.Speaker.tone(1250.0f, 18, -1, true);
}

void keyRelease() {
    if (!s_enabled) return;
    M5.Speaker.tone(820.0f, 14, -1, true);
}

void ok() {
    startSequence(kOk, sizeof(kOk) / sizeof(kOk[0]));
}

void taskDone() {
    // 有自定义结尾音就优先播它（放 SD: /dswhale/sound/task_end.wav）
    if (playWavFile("/dswhale/sound/task_end.wav")) return;
    startSequence(kDone, sizeof(kDone) / sizeof(kDone[0]));
}

void error() {
    startSequence(kErr, sizeof(kErr) / sizeof(kErr[0]));
}

bool playWavFile(const char* path) {
    if (!s_enabled) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    const size_t len = f.size();
    if (len < 44 || len > 4u * 1024u * 1024u) {
        f.close();
        return false;
    }
    if (!s_wavBuf || s_wavCap < len) {
        // 上一段还在播就不动缓冲区，避免把正在读的内存释放掉。
        if (M5.Speaker.isPlaying()) {
            f.close();
            return false;
        }
        if (s_wavBuf) free(s_wavBuf);
        s_wavBuf = (uint8_t*)allocPreferPsram(len);
        s_wavCap = s_wavBuf ? len : 0;
        if (!s_wavBuf) {
            s_wavCap = 0;
            f.close();
            return false;
        }
    }
    const size_t got = f.read(s_wavBuf, len);
    f.close();
    if (got != len) return false;
    return M5.Speaker.playWav(s_wavBuf, got, 1, -1, true);
}

}  // namespace sound
