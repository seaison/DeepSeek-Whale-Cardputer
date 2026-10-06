<!-- 感谢 PR！请把下面几项填一下，CI 会自动跑编译与自检 -->

## 改了什么

<!-- 一两句话。关联的 issue 用 "closes #123" -->

## 类型

- [ ] Bug 修复
- [ ] 新功能
- [ ] 移植上游能力（请贴上游对应的代码/文档位置）
- [ ] 文档 / CI / 工具
- [ ] 重构（行为不变）

## 验证方式

- [ ] `arduino-cli compile --fqbn "esp32:esp32:m5stack_cardputer:PSRAM=enabled,PartitionScheme=default_8MB" .` 通过
- [ ] 在真机上试过（请写清楚测了什么：哪个屏、哪个按键、什么网络环境）
- [ ] 涉及记账/峰谷/定价时，说明与上游口径的一致性
- [ ] 涉及 `assets/` 或生成的 `firmware/DeepSeekWhale/src/assets/*.h` 时，已跑 `python3 tools/bin2header.py …` 并 `diff`

## 真机验证记录

<!-- 设备、固件版本、Network 屏或串口的关键输出（api key 打码） -->

## 需要 reviewer 注意的地方

<!-- 兼容性、配置字段变化、内存/体积变化、许可证相关 -->
