# 第六轮复核固件上传记录（2026-10-02）

源码提交：`e289649`（包含第六轮修复提交 `b07f08e` 与诊断构建提交 `ebc4ddb`）。使用 `tools/build_esp32d.sh /tmp/cf-drone-e289649-upload`，目标为 ESP32-D `esp32:esp32:esp32:PartitionScheme=min_spiffs`。编译成功：程序 1,352,343 B / 1,966,080 B（68%），静态 RAM 124,548 B / 327,680 B（38%）。应用镜像为 [cf-drone-e289649-20261002.bin](deliverables/cf-drone-e289649-20261002.bin)，1,352,496 B，SHA-256 `6511f8147d8a688312bc99757f0706f844dc0781eb412a5824564dce2156efc4`。

目标设备 `/dev/cu.usbserial-10` 经 esptool 识别为 ESP32-D0WD-V3、MAC `20:50:0d:33:58:40`。上传前串口 `mot` 显示四路输出全零；`diag brief` 显示 `armed=0 imu_ok=1 motor_ok=1 battery_mv=4077 faults=0x00000000`。已将旧 app0 全部 1,966,080 B 读到 `/tmp/cf-drone-pre-e289649-app0.bin`，其前 1,352,464 B 与仓库内 [上一镜像](deliverables/cf-drone-light-loop-fast-stop-20261002.bin) 逐字节一致；临时备份 SHA-256 为 `c301eb26bc34b7b7b9cb1e36ebef8d250182735c0ead197756f0582932399e8d`。

仅在 `0x10000` 写入 app0 应用镜像，未改引导程序、分区表、NVS 或 SPIFFS。esptool 写入后内部哈希校验成功，另一次独立 `verify-flash` 对全部 1,352,496 B 返回 `digest matched`。复位后串口再次确认四路输出全零，`armed=0 imu_ok=1 motor_ok=1 battery_mv=4077 faults=0x00000000`。

本次网页状态请求超时，设备串口曾报告 Wi-Fi 正在重试连接；因此仅确认 USB 串口侧启动与安全状态。未解锁、未启动电机，未做迫降端到端时延或飞行验收。
