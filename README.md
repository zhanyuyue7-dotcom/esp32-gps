# TrackBox GPS 轨迹记录器

ESP32-S3 + GY-GPS6MV2/NEO-6M + 128×64 SPI OLED 的骑行/跑步轨迹记录器。OLED 驱动和 Atkinson Hyperlegible 数字字体复用 `gas` 项目；手机 Dashboard 的视觉系统复用 `esp32` 项目。

## 已实现

- GPS 经纬度、UTC、速度、海拔、卫星数、HDOP 解析
- 骑行/跑步模式
- 开始、暂停、继续、结束
- OLED 实时速度/配速、距离、时间、卫星状态
- 1 Hz 轨迹保存、Haversine 距离、弱定位/静止漂移/飞点过滤
- SPIFFS 本地持久化
- 手机热点 Dashboard、活动列表、活动详情、轨迹轮廓、速度曲线
- GPX/CSV 下载和二次确认删除

实物 NEO-6M 已诊断为 **38400 baud** 输出；固件使用该实测值，而不是常见默认值 `9600`。

## 操作

| 操作 | 按键 |
|---|---|
| 切换骑行/跑步 | READY 状态短按 `B` |
| 开始记录 | READY 状态短按 `A` |
| 暂停/继续 | 记录中短按 `A` |
| 保存并结束 | 记录中长按 `A` 2 秒 |

Wi-Fi：`TrackBox-XXXX`，密码 `trackbox1`，网页：`http://192.168.4.1`。

完整接线见 [docs/wiring.md](docs/wiring.md)。

## 构建与烧录

```powershell
Set-Location 'D:\Downloads\contentcreater-resources\esp32-gps'
& 'D:\Downloads\ESP-IDF\v5.5.3\esp-idf\export.ps1'
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

跨项目更换分区布局前先备份原板上的 NVS 和轨迹数据；不要默认执行全片擦除。烧录本项目会写入 bootloader、分区表和应用，请先确认目标确实是本项目的 ESP32-S3 板卡。

已经构建好的应用固件位于 `build/gps_trackbox.bin`。完整烧录还需要同目录生成的 bootloader 和 partition table，因此优先使用 `idf.py flash`。

## Host tests

```powershell
cmake -S host -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## GitHub CI 与交付 ZIP

`.github/workflows/ci.yml` 在 push / PR 时执行 Python 打包回归测试、C++/Web host tests 和 ESP-IDF 5.5.3 的 ESP32-S3 固件构建，全部通过后上传 `firmware-delivery` artifact（保留 90 天）。未配置 GitHub Pages 自动部署。

交付固定为 `delivery.json` 锁定的最新已烧录 BIN（2026-09-03），不是每次 CI 随意重新替换的固件。镜像构建校验和硬件实测是不同证据；本次交付没有重新接板实测。

```powershell
python -m unittest discover -s scripts -p 'test_*.py' -v
python scripts/package_delivery.py
```

生成 `dist/esp32-gps-delivery-20260906.zip`，仅含 `接线手册.md`、`bootloader.bin`、`partition-table.bin`、`gps_trackbox.bin`。手册附全部烧录地址、校验值、使用说明及嵌入字体的 OFL 许可证。ZIP 不含源码、缓存、日志或个人配置。

源码依赖目录均在本仓库，字体授权见 `third_party/atkinson-hyperlegible/OFL.txt`。后续替换固件时须重新核对接线与烧录记录，更新 `delivery.json` 的 SHA256 后再打包；不能仅为了绕过检查而更新 hash。
