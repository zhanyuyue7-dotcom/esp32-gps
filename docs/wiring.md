# TrackBox 接线说明

适用开发板：**ESP32-S3 双 USB-C、16 MB Flash 开发板**。交付基线是 2026-09-03 12:08:04 构建的 `gps_trackbox.bin`，12:09 的烧录日志确认该 1,122,064 bytes 应用镜像校验通过。

## 总接线表

### OLED（正面丝印：GND VCC SCL SDA RES DC）

| OLED | ESP32-S3 | 说明 |
|---|---|---|
| `GND` | `GND` | 共地 |
| `VCC` | `3V3` | **只能接 3.3 V** |
| `SCL` | `GPIO12` | SPI Clock |
| `SDA` | `GPIO11` | SPI MOSI，不是 I²C SDA |
| `RES` | `GPIO10` | OLED Reset |
| `DC` | `GPIO9` | Data/Command |

这是六针 SPI OLED，不是四针 I²C 屏。本固件不驱动 CS；六针模块须内部已使能 CS，七针模块需按模块资料将低有效 CS 固定为有效，不能悬空。不要直接套用别的项目的屏幕或中文字库。

### GY-GPS6MV2 / NEO-6M

| GPS | ESP32-S3 | 说明 |
|---|---|---|
| `VCC` | `5V/VBUS` | GPS 板带稳压，使用 5 V 输入 |
| `RX` | `GPIO4` | ESP32 TX → GPS RX；首版实际上可不接，但建议接好 |
| `TX` | `GPIO5` | **GPS TX → ESP32 RX，必须接** |
| `GND` | `GND` | 共地 |

这块实物 GPS 已通过串口诊断确认输出波特率为 **38400 baud**；固件已按该值配置，不要改回常见的出厂 `9600`。

### 两个按键

| 按键 | 一端 | 另一端 | 说明 |
|---|---|---|---|
| `A` | `GPIO6` | `GND` | 开始、暂停、继续、长按结束 |
| `B` | `GPIO7` | `GND` | 待机时切换骑行/跑步 |

固件已经启用 ESP32 内部上拉，**按键不需要外接电阻**。四脚轻触按键同一侧的两只脚通常内部相通，应使用按下前不导通的两侧脚；上电前用万用表通断档确认。

## 一眼照着接

```text
OLED GND  ───────── GND
OLED VCC  ───────── 3V3
OLED SCL  ───────── GPIO12
OLED SDA  ───────── GPIO11
OLED RES  ───────── GPIO10
OLED DC   ───────── GPIO9

GPS VCC   ───────── 5V / VBUS
GPS RX    ───────── GPIO4
GPS TX    ───────── GPIO5
GPS GND   ───────── GND

按键 A    ───────── GPIO6 与 GND
按键 B    ───────── GPIO7 与 GND
```

## 接线顺序

1. **拔掉 ESP32 USB 电源。**
2. 先连接 OLED、GPS 和两个按键的全部 `GND`。
3. OLED `VCC` 接 `3V3`，再接 `GPIO9～12` 四根信号线。
4. GPS `VCC` 接 `5V/VBUS`，然后交叉接 UART：`GPS TX → GPIO5`、`GPS RX → GPIO4`。
5. 两个按键分别接 `GPIO6/GND`、`GPIO7/GND`。
6. 插紧 GPS 的 IPEX 天线线头；陶瓷天线白色/裸陶瓷接收面朝天。
7. 万用表确认 `3V3` 与 `GND`、`5V` 与 `GND` 没有短路，再插 USB。

## 上电后的正常现象

1. OLED 先显示 `GPS SEARCH`。
2. 把设备拿到室外，陶瓷天线朝天；首次冷启动可能需要几分钟。
3. 定位完成后 OLED 显示 `GPS READY`。
4. 短按 `B` 在 `BIKE` 和 `RUN` 间切换。
5. 短按 `A` 开始；再次短按暂停/继续；**长按 A 2 秒保存并结束**。
6. 手机连接 `TrackBox-XXXX`，密码 `trackbox1`，打开 `http://192.168.4.1`。

## 常见接错表现

| 表现 | 优先检查 |
|---|---|
| OLED 全黑 | `VCC→3V3`、`SCL→GPIO12`、`SDA→GPIO11`、`RES→GPIO10`、`DC→GPIO9` |
| OLED 亮但一直 GPS SEARCH | `GPS TX→GPIO5` 是否接反；GPS 是否有 5 V；天线是否插紧且在室外 |
| 按键无反应 | 是否接在轻触按键同一侧两脚；正确方式是按下前不导通、按下后导通 |
| 手机找不到热点 | 确认开发板是 ESP32-S3 且固件已经成功烧录 |
| 网页打不开 | 手机保持连接 TrackBox 热点，手动输入 `http://192.168.4.1`，不要使用 HTTPS |

## 数据与使用边界

默认热点密码 `trackbox1` 是公开演示密码；轨迹可能包含位置隐私，不向不可信用户开放热点。轨迹保存在板内 SPIFFS，改分区或清空 Flash 前先通过网页导出。GPS 用于轨迹记录，不替代专业导航或生命安全设备。
