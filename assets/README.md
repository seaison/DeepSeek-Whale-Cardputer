# assets/ — 鲸鱼位图

## 文件

| 文件 | 说明 |
|---|---|
| `whale_96x96_rgb565.bin` | 96×96 的**裸 RGB565 小端**位图，无文件头，正好 `96×96×2 = 18432` 字节 |

它由上游 `MeteorNOX/DeepSeek-Balance-Whale-Widget` 的 `assets/DSniang1.png`（小鲸鱼本体）
在本机处理后得到（缩放 + 抠图到纯黑底 + 转 RGB565）。固件里用的
`firmware/DeepSeekWhale/src/assets/whale_96.h` 就是从这个 bin 生成的。

## 许可

⚠️ **本目录的位图不适用 MIT 许可。** 上游把代码与美术素材分开授权：素材按 **as-is** 随包分发，
不授予再许可，也不声明为原创（详见上游 `PROVENANCE.md`）。本工程沿用同一声明，
完整说明见仓库根目录的 [NOTICE.md](../NOTICE.md)。

如果你要把它用在别的项目里，请先自行确认权利状态。

## 用工具复核 / 重新生成

```bash
# 1) 还原成 PNG 肉眼看（尺寸或字节序猜错时一眼就能发现）
python3 tools/bin2png.py \
  --input assets/whale_96x96_rgb565.bin \
  --output /tmp/whale_96.png \
  --width 96 --height 96 --format rgb565 --scale 3

# 2) 生成固件用的 C 头文件（CI 会 diff 这个结果，保证仓库里那份和 bin 一致）
python3 tools/bin2header.py \
  --input assets/whale_96x96_rgb565.bin \
  --output firmware/DeepSeekWhale/src/assets/whale_96.h \
  --width 96 --height 96 --name whale_96 --format rgb565
```

## 换成自己的图

`bin2header.py` 支持 `rgb565` / `rgb565be` / `gray8` / `mono1` 四种格式，尺寸与数组名都是参数，
所以换角色图不用改代码逻辑：

```bash
# 例：128x128 的灰度图
python3 tools/bin2header.py --input my.bin --output firmware/DeepSeekWhale/src/assets/my_whale.h \
  --width 128 --height 128 --name whale_128 --format gray8
```

然后改 `firmware/DeepSeekWhale/src/ui.cpp` 里 `#include "assets/whale_96.h"` 与 `Ui::whale()` 的尺寸宏即可。
（`M5GFX::pushImage` 对 `uint16_t` 走 RGB565 路径，`uint8_t` 需要先指定色深或调色板，
所以非 RGB565 的图目前还要自己补一步转换。）
