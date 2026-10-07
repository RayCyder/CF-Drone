# ESP32-D 三种传感器编译模式

ESP32-D 使用同一套电机、IMU、Wi-Fi、Web、遥控和安全代码，只按实物传感器组合裁剪可选驱动。编译模式由 `CF_DRONE_ESP32D_SENSOR_PROFILE` 指定，默认是模式 3 `full`。

## 模式矩阵

| 模式 | 脚本名称 | 数值 | IMU | BMP388 | QMC5883P | PMW3901 | VL53L1X | 用途 |
| --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| D1 | `imu` | 1 | 开 | 关 | 关 | 关 | 关 | 只有主板与 IMU |
| D2 | `baro-mag` | 2 | 开 | 开 | 开 | 关 | 关 | H6 接气压计、磁力计，没有 H5 光流和下视测距 |
| D3 | `full` | 3 | 开 | 开 | 开 | 开 | 开 | 完整传感器扩展板，H5/H6 都接入 |

三个模式都保留 GPIO33 为 PMW3901 片选，并在初始化 IMU 前置高。D1/D2 即使没有启用光流驱动，也能避免一个已连接或悬空的低有效片选干扰共享 SPI MISO。

## 编译命令

脚本第二个参数是传感器模式，第一个参数是构建目录：

```sh
# D1：只有 IMU
./tools/build_esp32d.sh /tmp/cf-drone-esp32d-imu imu

# D2：IMU + BMP388 + QMC5883P
./tools/build_esp32d.sh /tmp/cf-drone-esp32d-baro-mag baro-mag

# D3：完整扩展板；也是默认模式
./tools/build_esp32d.sh /tmp/cf-drone-esp32d-full full
```

也可通过环境变量选择，便于自动化构建：

```sh
ESP32D_SENSOR_PROFILE=baro-mag ./tools/build_esp32d.sh /tmp/cf-drone-build
```

允许的别名是：

- D1：`1`、`imu`、`imu-only`
- D2：`2`、`baro-mag`、`baro_mag`、`navigation`
- D3：`3`、`full`、`full-sensors`、`full_sensors`

不传模式时使用 D3，保持原来 ESP32-D 完整扩展板构建行为。

## Arduino IDE 或直接 Arduino CLI

没有额外宏时默认 D3。需要 D1/D2 时，在编译器 C++ extra flags 中加入：

```text
-DCF_DRONE_ESP32D_SENSOR_PROFILE=1
-DCF_DRONE_ESP32D_SENSOR_PROFILE=2
```

直接命令示例：

```sh
arduino-cli compile \
  --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs \
  --build-property 'compiler.cpp.extra_flags=-DCF_DRONE_ESP32D_SENSOR_PROFILE=2' \
  .
```

## 运行时识别

启动串口会输出当前固件实际编入的能力，例如：

```text
BOARD sensor_profile=esp32d-baro-mag barometer=1 compass=1 optical_flow=0 downward_range=0
```

每个脚本构建目录还会生成 `cf-drone-build-profile.txt`，记录板型、模式名称、模式编号和 FQBN。复制或重命名 `.bin` 时应一并保存该文件，避免把 D1/D2 镜像刷入完整扩展设备后误判传感器故障。

## 各模式的运行行为

### D1：仅 IMU

- 不初始化外部 I²C 传感器任务。
- 气压定高、磁航向、光流位置和下视测距不可用。
- STAB/ACRO 和基础安全逻辑继续工作；依赖高度或导航能力的模式必须由运行时能力检查拒绝。

### D2：气压计与磁力计

- 初始化 I²C、BMP388 和 QMC5883P。
- 支持气压高度和通过校准、实时可信度门控后的磁航向。
- 不编译 PMW3901/VL53L1X 采样路径；不能提供光流水平定位或测距辅助高度。

### D3：完整传感器

- 初始化 BMP388、QMC5883P、PMW3901 和 VL53L1X。
- VL53L1X 可参与有效量程内的垂直融合；PMW3901 与测距可生成水平速度/位置影子状态。
- 当前仍未完成 POSHOLD 水平闭环和自动触地，因此“编译为 full”只表示驱动被编入，不等于相关飞行功能已经通过验收。

## 兼容与刷写规则

- D1/D2/D3 使用相同 ESP32-D 引脚、电机布局、分区表和 NVS 数据格式，可以只更新 app0。
- 可选传感器是否编入与传感器是否探测成功是两层状态。D2/D3 中某个芯片未找到时应报告 not_found，不得伪装成 ready。
- 固件模式必须按实物选择。为完整扩展板刷 D1/D2 不会自动升级能力；为裸板刷 D3 虽会运行时探测缺失设备，但增加无意义初始化和诊断噪声。
- 三个模式均不能绕过 IMU、加速度计校准、水平校准、电池和其他既有解锁门槛。

## 建议的产物命名

```text
cf-drone-esp32d-d1-imu-<commit>.bin
cf-drone-esp32d-d2-baro-mag-<commit>.bin
cf-drone-esp32d-d3-full-<commit>.bin
```

每次交付同时记录 commit、构建时间、镜像大小、SHA-256 和 `cf-drone-build-profile.txt`。
