# 固件体积与静态存储复核（2026-10-04）

## 范围

本轮复核当前 ESP32-D 默认构建，重点检查程序分区占用、`.data`、`.bss`、RTC 保留区、项目内最大静态对象，以及任务栈和按需堆缓冲对剩余 RAM 的影响。基线为 `be0299b`，同时检查其后的 H6 磁力计校准工作区改动。

## 构建结果

| 项目 | `be0299b` 基线 | 本轮修复后 | 变化 |
| --- | ---: | ---: | ---: |
| 程序存储 | 1,372,855 B | 1,376,627 B | +3,772 B |
| Arduino 全局 RAM | 124,572 B | 124,380 B | -192 B |
| 剩余 RAM | 203,108 B | 203,300 B | +192 B |
| `.dram0.data` | — | 70,212 B | — |
| `.dram0.bss` | — | 54,168 B | — |
| `.rtc_noinit` | — | 2,288 B | — |
| `.flash.text` | — | 873,628 B | — |
| `.flash.rodata` | — | 345,368 B | — |

程序分区使用率为 70%，全局 RAM 使用率为 37%。当前没有接近链接上限，但 Web 页面和外部传感器功能是主要 flash 增量来源。

## 最大项目对象

| 对象 | 大小 | 存储区域 | 约束 |
| --- | ---: | --- | --- |
| `webRCIndexHtml` | 119,527 B | flash rodata | 不占普通 RAM |
| `flightLog` | 32,152 B | DRAM | 记录数组已有 32,000 B 静态断言 |
| `descentCalibration` | 12,248 B | DRAM | 本轮增加 12 KiB 总体预算断言 |
| `consoleBuf` | 11,040 B | BSS | 本轮增加 11,280 B 预算断言 |
| `openLoopBuffers` | 3,072 B | BSS | 本轮增加 3,072 B 预算断言 |
| `loopTrace` | 2,492 B | BSS | 固定容量 |
| `slowLoopRetentionStore` | 2,288 B | RTC noinit | 与普通 DRAM 分离 |

## 已修复问题

磁力计运行状态原先在启动时用 `new` 分配并永久保留。该对象是固定生命周期状态，却隐藏在 ELF 静态统计之外，还会让传感器初始化依赖堆分配。本轮改回固定静态对象，并用 64 B 编译期预算约束；实际静态 RAM 增加 40 B。ESP32-D 控制台历史从 47 行缩为 46 行，释放 240 B，因此本轮工作区相对基线仍净减少 192 B 全局 RAM。

同时为下降标定、Web 控制台和开环序列双缓冲补充编译期大小断言，后续容量扩大若突破预算会直接编译失败。

## 运行时 RAM 边界

Arduino 的全局 RAM 报告不包含运行时创建的任务栈、Wi-Fi/WebServer 内部堆和临时 `String`。应用显式创建的任务栈合计 31,744 B：五个 4,096 B 任务、一个 3,072 B 快速停止任务和一个 8,192 B HTTP 任务。IMU 采集按需分配约 24 KiB，原始陀螺模式约 36 KiB；控制台快照最坏还会临时分配 11,040 B。现有采集入口用 48/64 KiB 空闲堆门槛拒绝低内存启动。

静态布局通过，但仅凭链接结果不能证明 Wi-Fi 高负载、原始 IMU 采集和大控制台响应并发时的最低堆水位。后续硬件压力测试应记录 `ESP.getMinFreeHeap()`；本轮没有刷机或制造并发负载。

## 验证

- `python3 tests/run_host_tests.py`：通过。
- `tools/build_esp32d.sh /private/tmp/cf-drone-static-size-review-fixed-build`：通过。
- `xtensa-esp32-elf-size -A` 与 `xtensa-esp32-elf-nm --size-sort`：已核对段大小和最大符号。
- `git diff --check`：通过。

结论：修复固定状态的永久堆分配后，本轮静态存储复核通过。剩余关注点是运行时最低堆水位，不是当前链接期静态空间不足。
