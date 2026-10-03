# ESP32-S3 固件上传记录（2026-10-04）

目标设备经 ROM 下载器确认是 ESP32-S3 QFN56 revision 0.2，MAC `98:c3:77:64:6c:cc`，内置 4 MB Flash 与 2 MB QSPI PSRAM。源码提交为 `a98d991`。

S3 板级配置明确设置 `BOARD_BAROMETER_ENABLED=0` 与 `BOARD_COMPASS_ENABLED=0`。启动时不初始化 I²C，也不探测 BMP388 或 QMC5883P；ESP32-D 的传感器配置不受影响。S3 控制台历史由 50 行调整为 46 行，以满足现有 `11280 B` 静态缓冲预算。

构建目标为 `esp32:esp32:esp32s3:PartitionScheme=min_spiffs,PSRAM=enabled,CDCOnBoot=cdc`。程序占用 `1,364,006 B / 1,966,080 B`，静态 RAM `123,672 B / 327,680 B`。app 镜像为 [cf-drone-a98d991-s3-no-baro-mag-20261004.bin](deliverables/cf-drone-a98d991-s3-no-baro-mag-20261004.bin)，大小 `1,364,160 B`，SHA-256 `2dcd8c4ab5ab15286a413a20ab1229e97f6dce643126e00bddc7390b370d4fe9`。

设备原 `0x8000` 分区表区域为空，因此本次写入 S3 bootloader、`min_spiffs` 分区表、OTA 引导和 app0，没有擦除整片 Flash。Arduino 上传阶段逐段校验成功；随后使用 esptool 独立校验 `0x0`、`0x8000`、`0xe000` 与 `0x10000`，四段均返回 `digest matched`。

写入后 USB Serial/JTAG 端口重新枚举。尝试发送 `diag brief`、`sensors` 与 `sys` 时没有收到应用控制台文本，因此本记录只能证明目标身份、构建配置、写入内容和 Flash 校验，不能证明应用主循环、IMU、电机输出、上电电机自检或 Web 服务的运行状态。需要后续取得串口或 Web 状态后再完成运行期验收。
