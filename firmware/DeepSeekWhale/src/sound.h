// 音效：内置合成音（不占空间、不涉及素材授权），另支持从 SD 卡播放 wav。
//
// 上游插件的 mp3/wav 素材**不在 MIT 许可范围内**（见上游 PROVENANCE.md），
// 所以本工程不打包那些音效；想用的话把它们放到 SD 卡的 /dswhale/sound/ 下：
//   /dswhale/sound/task_end.wav   -> 任务结束音（16bit PCM wav）
//   /dswhale/sound/key.wav        -> 按压音
#pragma once

#include <stdint.h>

namespace sound {

void begin(bool enabled, uint8_t volume);
void loop();  // 推进合成音的序列（非阻塞）

void setEnabled(bool on);
bool enabled();
void setVolume(uint8_t vol);
uint8_t volume();

void keyPress();
void keyRelease();
void ok();
void taskDone();
void error();

// 从 SD 读取 wav 播放（整个文件读进 PSRAM，播放期间保持存活）。
bool playWavFile(const char* path);

}  // namespace sound
