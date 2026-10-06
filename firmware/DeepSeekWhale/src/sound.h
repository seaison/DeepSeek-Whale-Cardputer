// 音效：内置合成音（不占空间、不涉及素材授权），另支持从 SD 卡播放 wav。
//
// 上游插件的 mp3/wav 素材**不在 MIT 许可范围内**（见上游 PROVENANCE.md），
// 所以本工程不打包那些音效；想用「插件同款声音」就自己转成 wav 放进 SD 卡：
//   /dswhale/sound/press.wav      -> 按键按下（叫 keyPress）
//   /dswhale/sound/release.wav    -> 按键松开 / 返回
//   /dswhale/sound/click.wav      -> 点按鲸鱼（插件里的「按压音效」）
//   /dswhale/sound/task_end.wav   -> 任务结束音
// 文件存在就用它，不存在就回落到内置合成音。转法：
//   ffmpeg -i Ya1.mp3 -ac 1 -ar 16000 -sample_fmt s16 click.wav
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
void whaleClick();  // 点按鲸鱼：和普通按键音区分开（插件里对应「按压音效」）
void ok();
void taskDone();
void error();

// 从 SD 读取 wav 播放（整个文件读进 PSRAM，播放期间保持存活）。
bool playWavFile(const char* path);

}  // namespace sound
