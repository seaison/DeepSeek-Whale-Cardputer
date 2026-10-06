# 上手、烧录与排错

## 一、准备

1. **M5Stack Cardputer ADV** + USB-C 数据线（能传数据的那种）
2. **Arduino IDE 2.x**（或 arduino-cli）
3. **arduino-esp32 core 3.3.x**：开发板管理器里搜 `esp32`（M5Stack 官方现在也要求用 espressif 的 core）
4. 库（库管理器里装，或 `arduino-cli lib install`）：
   - `M5Cardputer`（≥ 1.1.1，**只有这个版本才认 ADV 的 TCA8418 键盘**）
   - `M5Unified`（≥ 0.2.25）
   - `M5GFX`（≥ 0.2.32）
   - `ArduinoJson`（≥ 7.x）
5. microSD 卡（**FAT32 + MBR**，见下一节），强烈建议插上

### 打开工程

sketch 在 `firmware/DeepSeekWhale/`（Arduino 要求 **sketch 目录名与 `.ino` 同名**，
所以仓库根目录不能直接当 sketch——仓库名带连字符）：

```bash
git clone https://github.com/seaison/DeepSeek-Whale-Cardputer.git
# Arduino IDE：File → Open → DeepSeek-Whale-Cardputer/firmware/DeepSeekWhale/DeepSeekWhale.ino
```

克隆到哪个目录名都无所谓——只要打开的是 `firmware/DeepSeekWhale/` 这一层。

## 二、SD 卡怎么格式化

固件用的是 ESP-IDF 的 FatFs，这份编译配置（`ffconf.h`）里有两条硬限制：

| 配置 | 值 | 含义 |
|---|---|---|
| `FF_FS_EXFAT` | **0** | **不支持 exFAT** |
| `FF_LBA64` | **0** | 只认 **MBR** 分区表，不认 GPT |
| `FF_SS_SDCARD` | 512 | 512 字节逻辑扇区（SD 卡本来就是） |

所以：

- ✅ **FAT32**（推荐；≤2GB 的老卡 FAT16/FAT12 也行）
- ❌ exFAT —— 挂载会失败（`SD.begin` 返回 false，菜单 → 网络 里显示 `SD 卡 未插入`）
- ❌ NTFS / ext4 / APFS
- ❌ **GPT 分区表**（哪怕里面是 FAT32）—— 请用 **MBR**

簇大小、卷标随便，用默认值就行。**空卡就行**，`/dswhale/` 目录是固件自己建的。
固件**不会**格式化卡（`SD.begin(..., format_if_empty=false)`），格式不对就直接不挂载。

### macOS

```bash
diskutil list                      # 找到卡的标识，比如 /dev/disk4（别选错，会抹掉整盘）
diskutil unmountDisk /dev/disk4
sudo diskutil eraseDisk MS-DOS DSWHALE MBR /dev/disk4     # MBR + FAT32
```

64GB 以上的卡如果 `MS-DOS` 落到 exFAT，就显式指定：

```bash
sudo diskutil partitionDisk /dev/disk4 MBR FAT32 DSWHALE 100%
```

### Windows

- **≤32GB**：右键 → 格式化 → 文件系统选 **FAT32**（默认簇大小）
- **>32GB**：Windows 自带对话框只给 exFAT/NTFS → 用 [Rufus](https://rufus.ie)（选「大 FAT32」）
  或 [FAT32 Format](http://ridgecrop.co.uk/index.htm?guiformat.htm)

### Linux

```bash
sudo parted /dev/sdX mklabel msdos          # MBR
sudo parted -s /dev/sdX mkpart primary fat32 0% 100%
sudo mkfs.vfat -F 32 -n DSWHALE /dev/sdX1
```

### 两个容易踩的点

1. **卡要在开机前插好**：固件只在 `setup()` 里挂载一次，运行中插卡不会重新识别，得重启。
2. **TF 转 SD 卡套上的写保护开关**要拨到可写——锁上时 `SD.begin` 仍会成功，但写 `config.json` / `ledger.json` 会失败（菜单里保存会弹 `保存失败`）。

## 三、开发板设置

Arduino IDE → **工具**：

| 选项 | 值 | 为什么 |
|---|---|---|
| Board | **M5Cardputer** | ADV 没有单独的板型，运行时会自动识别成 `board_M5CardputerADV` |
| PSRAM | **QSPI PSRAM** | 240×135×16bit 的离屏缓冲约 65KB，放 PSRAM 里给 TLS 留出内部 RAM |
| Partition Scheme | **8M with spiffs (3MB APP/1.5MB SPIFFS)** | ⚠️ **必选**：默认 4MB 分区只有 1.2MB APP，这个固件约 2.48MB（中文字库占约 1.08MB），**装不下** |
| USB CDC On Boot | **Enabled** | 串口日志 |
| Upload Speed | 921600 或更高 | 快一点 |

CLI 等价写法：

```
--fqbn "esp32:esp32:m5stack_cardputer:PSRAM=enabled,PartitionScheme=default_8MB"
```

## 四、烧录

```bash
# 看端口
arduino-cli board list

# 编译
arduino-cli compile --fqbn "esp32:esp32:m5stack_cardputer:PSRAM=enabled,PartitionScheme=default_8MB" firmware/DeepSeekWhale

# 上传
arduino-cli upload  --fqbn "esp32:esp32:m5stack_cardputer:PSRAM=enabled,PartitionScheme=default_8MB" -p /dev/cu.usbmodem1101 DeepSeekWhale
```

> 卡在 `Connecting...` 就按住侧面的 **G0** 再点上传，或者拔掉再插一次。设备上有复位键，别用拔线当复位。

## 五、第一次开机

顺序是：挂载 SD → 读配置 → 连 WiFi（最多 15 秒）→ NTP 对时（最多 12 秒）→ 拉一次余额 → 主界面。

- 屏幕左上是 WiFi 圆点：绿=已连，橙=在连，红=没配对
- 右上徽标 `PEAK` / `OFF-PEAK` / `SYNC TIME`
- 主屏右下是当前档位的**下一切换倒计时**

## 六、排错

### 屏幕上没有余额，只显示 `--`

按 `TAB` → `Network`，那一屏把所有诊断信息列全了：

| 看到的 | 说明 | 怎么办 |
|---|---|---|
| `state  no ssid` | 没读到 WiFi 配置 | 检查 SD 卡里 `/dswhale/config.json` 是否存在、字段是否是 `wifi.ssid` |
| `state  connecting` 一直不变 | 连不上 | 确认是 **2.4GHz** WiFi（ESP32 不支持 5GHz）；密码对不对；路由器有没有开 MAC 过滤 |
| `api key  missing` | `api_key` 没配 | 填进 config.json，菜单里 `Reload config` |
| `last fetch  HTTP 401` | key 无效 | 去 platform 重新生成，注意别把 `Bearer ` 前缀写进配置 |
| `last fetch  HTTP 402` | 余额不足 | 充值 |
| `last fetch  net err -1` | TLS/网络失败 | 先看 `time sync` 是不是 `pending`；被代理/校园网中间人时把 `tls_verify` 设成 `false` |
| `time sync  pending` | NTP 没成功 | 换个能出网的环境；NTP 用的是 `ntp.aliyun.com` / `ntp.tencent.com` / `pool.ntp.org` |
| `sd card  not present` | 没识别到卡 | 卡要 **FAT32 + MBR**（exFAT/GPT 挂不上，见第二节）；开机前插好；重新插紧；换个卡试 |

### 今日已用是 `--`

**这是正常的**：记账需要至少两次余额观测才有意义——第一次观测只是**统计起点**。

另外「今日已用」是**已观测消费**：它从当天第一次观测开始算，起点之前的消费不在区间内。所以会看到屏幕上的数字比官网小——上游挂件也有同样的口径问题，它的 `partialDay` 标记就是干这个的（本工程里「部分数据」会用淡蓝色显示，而不是纯白）。

### 数字和官网对不上

先核对：**同一个账号、同一种币种、同一个时间区间**。余额接口只返回快照、不提供充值流水，所以**充值和消费发生在同一次刷新间隔**时必然失真（上游同样如此，它靠手动「余额校正」处理）。

### 屏幕白屏 / 花屏

- 确认 Partition Scheme 和 PSRAM 按上表设置；
- 确认 `M5Cardputer` 库是 **1.1.1+**（老版本在 ADV 上键盘和显示都会出问题）；
- 串口看有没有 `离屏缓冲创建失败`。

### 编译报分区装不下

```
Sketch uses ... Maximum is 1310720 bytes
```

→ 把 Partition Scheme 换成 **8M with spiffs (3MB APP)**。

### 键盘没反应

- 只有 ADV 才走 TCA8418；确认库版本；
- 串口里如果打印 `Keyboard: Unsupported board type`，说明 M5Unified 没识别成 `board_M5CardputerADV`——升级 M5Unified / M5GFX。

## 七、按键速查

| 按键 | 作用 |
|---|---|
| `ENTER` / `空格` | 主屏冒泡；菜单确认 |
| `TAB` | 开/关菜单 |
| `` ` `` / `DEL` | 返回 |
| `;` `w` / `.` `s` | 上下（长按连发）；ADV 上也可用 `FN` + `;` `.` `,` `/` |
| `,` `a` / `/` `d` | 左右（设置项调值） |
| `R` | 立即刷新余额 |
| `L` | 中英切换（菜单 → 设置 → 语言 也能切） |
| `ENTER` / 空格 | 点按鲸鱼：冒泡 + 回弹一下（Q 弹）+ 鲸鱼点击音（可在设置里关掉动画） |

## 八、串口日志

115200 波特率。关键行：

```
[whale] DeepSeek Whale 1.0.0 for Cardputer ADV
[whale] SD: SDHC 29.7GB
[net] WiFi 已连接 192.168.1.23 RSSI=-52
[net] NTP 对时完成 t=1791234567
[whale] 账本已载入: 12 天 1 本
```
