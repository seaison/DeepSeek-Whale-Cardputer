// 鲸鱼气泡：移植上游挂件的「点击序列」——
// 第一次点角色看到余额，再点进入队列，队列走完开始抽随机台词；
// 一段延时（默认 30s）不点就回到第 1 项。
#pragma once

#include <string>
#include <vector>

#include "ui.h"

namespace bubbles {

struct Bubble {
    std::string title;
    std::vector<std::string> lines;
};

// 是否已经点过（用于判断要不要重置计时）
void reset();
bool active();
void touch();

// 推进一格并生成内容。
Bubble next(const ViewModel& vm);

// 只取随机台词，不动队列（菜单里「随机台词」用）。
Bubble random_(const ViewModel& vm);

}  // namespace bubbles
