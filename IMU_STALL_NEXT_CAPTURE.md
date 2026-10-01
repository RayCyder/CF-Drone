# 下一轮 IMU 等待与重启诊断方案

状态：诊断方案已完成；RTC 保留记录尚未实现。当前飞控运行已验证的 app 镜像；现场试验继续限定在用户确认的拆桨固定架。台架脚本现于上锁后优先导出独立保留的最坏循环，再下载冻结日志，避免滚动 trace 覆盖告警现场。

## 目前最可能的路径

优先怀疑 `CF-Drone.ino::loop()` 调用 `readIMU()` 后进入 `IMUBase::waitForData(5)`，而不是控制律本身：

1. `setupInterruptTimer()` 在没有接 DRDY 引脚的条件下，以约 1 kHz GPTimer 产生信号量；周期为按 IMU 采样率取整的 1 ms 再加 10 us 相位余量。
2. `waitForData()` 等信号量后立即尝试 MPU `read()`；若 MPU 的 `RAW_DATA_RDY` 尚未置位，会再轮询最多约 200 us，然后才等下一次 GPTimer。等待时长因此对 GPTimer 与 MPU 内部采样相位敏感。
3. 正常生产路径记录 IMU 阶段的总墙钟时间，不能把它分解为定时器相位、任务就绪延迟或 SPI 传输时间。完整 trace 构建额外记录这些分量，但最近两次适合比较的材料没有保留下可用事件：033428 是普通 trace 格式，040042 没有超过阈值的 worst trace；041308 完成航线并上锁后 `/logs/status` 超时、板端随后重启，RAM 环形记录随之丢失。

已观测到的 1.717–1.718 ms 最坏循环包含 1.027–1.071 ms `imu_wait`。这使 IMU 等待路径成为最直接的待证假设，但不能单凭阶段计时排除 ISR/临界区延迟、任务被切出、SPI 读耗时或其它中断。10 秒 30% 无桨运行中没有超过 1.5 ms 的 trace，只能说明该次没有触发记录条件；不代表长尾问题已经消失。

### 新增证据：045010 四电机记录

`data/attitude/four-motor-30pct-20261002-045010-loop-worst.json` 在 uptime `49.711 s` 记录到 `dt_us=1528`（loop sequence `47892`）。阶段序列中 `imu_wait=156 us`、`rc_web=456 us`、`estimate=142 us`、`control_law=342 us`、`motor_out=79 us`、`serial_input=108 us`；因此这次 1.528 ms 告警不支持把所有长循环归因于 IMU 等待。它刚超过 1.5 ms 阈值，且不同阶段的墙钟耗时同时偏高，可能是多个普通阶段成本累积，也可能是某段被抢占后把墙钟时间算在该阶段。现有 worst JSON 没有同期 scheduler、IPC、IMU 细分字段，也没有每个 RC 子操作的时长，无法在这两种解释间选择。它没有推翻此前 IMU 等待事件的观察，只表明至少存在另一类慢循环表现。

因此下一次诊断的捕获触发应覆盖所有 `dt >= 1500 us`，而不是只捕获 IMU wait 长事件；每条捕获保留完整 16-stage 数组并关联同序列的 scheduler/IPC。分析时先检查 `rc_web` 阶段是否有任务切出或 IPC 重叠，再检查控制阶段；只有 scheduler/IPC 不能解释、且 IMU wait 分量显著时，才归入 IMU 路径继续拆分。`rc_web=456 us` 是整个阶段的标记，不等于 `/web_rc` handler 执行时间，必须与 route-specific HTTP 指标及任务切换证据分开解释。

重新调平后的 30%、10 秒无桨航线 `061106` 再次捕获到另一种分布：[最坏循环](data/attitude/prop-off-route-20261002-061106-loop-worst.json)为 `1669 us`，其中 `imu_wait=1034 us`、`rc_web=80 us`、`control_law=193 us`。同次航线已完成三段并上锁，已解锁日志无漏采；低频姿态采样见 [控制记录](data/attitude/prop-off-route-20261002-061106.jsonl)。因此 045010 的 Web/控制律型与 061106 的 IMU 等待型都在当前装置上重复出现，后续捕获仍须关联同一慢循环的 scheduler/IPC/IMU 细分数据，不能只优化其中一种阶段。

## 仍缺少的证据

- 同一条慢循环上的 IMU 等待字段：`waitStartedUs`、`waitEndedUs`、GPTimer ISR 计数增量和最后 ISR 时间、信号量 take/timeout/最大等待、`read()` 次数/成功次数/总时长/最大时长。
- 慢循环区间内 flight-loop 的 scheduler switch-out/in 事件及其它任务身份；现有 1.7 ms stage 数字是墙钟时间，不是 CPU 执行时间。
- 该区间是否与 flash-cache IPC 回调重叠；既往已证明启动期 flash callback 可独立造成约 51 ms 停顿，但当前循环/运行记录没有同一事件的 IPC 证据。
- 041308 重启的芯片 reset reason、启动串口记录、启动前后供电波形/最低电压。航线记录只证明上锁和零输出发生在 HTTP 超时之前，不能确定之后是看门狗、软件重启、棕断、外部复位还是其他原因。
- MPU 的真实 DRDY 引脚/信号相位。此机型目前使用软件 GPTimer 替代无接线 DRDY；因此最多只能用传感器 data-ready 状态与软件定时器时间间接估算相位，不能声称已测到真实 DRDY 边沿。

## 最小下一次诊断

### 记录内容与保存

保留现有 task-trace 构建中的 scheduler、IMU wait summary 和 flash IPC longest-callback 记录，不启用逐循环打印、网络实时流或更密集采样。只新增一个小型、定长、无堆分配的保留区记录：

- 仅保存触发阈值以上事件的完整 loop trace、对应 scheduler/IPC 事件，以及一个单调递增 capture id；触发后冻结，避免后续正常循环覆盖。
- 每条记录写入 RTC 保留 RAM 的双槽/序号/CRC 格式；启动早期先校验并打印 `capture id + reset reason + records + CRC`，确认已从串口采集后才允许清除。启动过程中不得在飞行路径写 NVS，也不在 ISR 中写 flash。
- 保留一份小的启动头：复位原因数值、启动计数、前次 capture id、记录完整性标志。现有 `BOOT reset=...` 串口输出可作为外部证据，但需确认串口采集器在重启后持续记录；单靠丢失的 HTTP session 不够。
- 若 RTC 保留 RAM 无法满足目标芯片/复位类型的持久性，使用外部主机持续保存串口启动日志；不要为此在 armed 状态下同步写 flash。棕断可能令 RTC 内容不可信，CRC 失败也必须作为证据记录而非覆盖。

这是建议的最小代码增量，不是本次已实施修改。实现前需在任务中审阅具体结构大小、复位类型保留行为和启动清理时序。不得改变传感器等待语义、控制律、输出或 failsafe。

### 一次捕获流程

1. 先用拆桨固定架；保存当前镜像摘要、任务版本/构建 ID，启动外部串口连续采集，并确认前次 RTC capture 已导出或为空。
2. 若启用诊断构建，先记录镜像摘要，只写 app 分区并回读校验，随后核对上锁、电机零输出和参数持久化。
3. 先做短时 disarmed 基线，比较 trace 开/关的循环开销与 >1.5 ms 事件计数；若诊断开启本身增加长尾或产生新 fault，停止本轮，不进入电机运行阶段。
4. 只有在基线无新增异常时，才按已确认的拆桨固定架条件执行低油门运行。触发时让 recorder 冻结，不发并行的大型日志 HTTP 导出请求。
5. 确认上锁、四路输出为零后，由主机先保存串口中冻结记录，再做小型 HTTP 导出；记录每次请求的 connect/send/headers/body 耗时和 HTTP 状态。导出失败不重启设备、不清除 RTC 区。
6. 若设备自行重启，串口采集器记录重启前尾部与重启后的完整 boot banner/reset reason；重启后先读出并校验 RTC capture，再做任何可能覆盖它的动作。重启原因不明时，先保持拆桨、上锁与电机零输出，完成证据保存后再决定是否继续台架运行。

## 如何区分候选原因

| 候选原因 | 慢循环应看到的信号 | 可下的结论 / 限制 |
|---|---|---|
| GPTimer 唤醒或信号量路径 | `imu_interrupt_source=GPTIMER`；ISR 计数未增长、最后 ISR 时间明显早/晚，或 ISR 已到但 `semaphoreWaitMaxUs` 很长；结合 scheduler trace 看 task 何时被切出/恢复 | ISR 缺失/迟到支持 timer/中断投递延迟；ISR 准时而 semaphore wait/任务恢复迟支持调度或信号量唤醒延迟。若 trace 没记录 ISR 屏蔽区，仍需更低层硬件/中断追踪。
| 软件定时器与 MPU data-ready 相位 | ISR 准时；首次 `read()` 未 ready；随后短轮询成功，或错过窗口后等到下一个 ISR 才 ready；慢等待可能接近一个周期 | 这支持相位/轮询采样问题。因为没有接线 DRDY，不能从软件 GPTimer timestamp 得到真实 DRDY 边沿；要直接证实需后续安全的 DRDY 接线/逻辑分析仪方案。
| SPI 读取耗时 | `readTotalUs` 或 `readMaxUs` 与 wait/span 同量级；`readAttempts` 能指出多次探测；scheduler/IPC trace 没有覆盖主要区间 | 支持传感器/SPI 访问或底层总线等待。若只有小于数十微秒的读耗时，则 SPI 不能解释约 1 ms 的 wait。
| 任务抢占/flash IPC | loop task 在慢区间 switch-out，别的 task 运行；或 IPC callback 覆盖同区间 | scheduler 事件可归因到具体任务；IPC 记录可进一步关联调用者/目标核。当前已知历史上的约 51 ms NVS-cache stall 是启动期现象，不能外推为本次 armed 问题。
| loop task 没切出但墙钟异常 | wait/read 标记不能覆盖该区间，且 scheduler 没有有效 switch-out/in | 说明可能是 ISR、关中断/临界区或未标记的同步路径；task-switch trace 单独不能区分，下一步才考虑有针对性的 ISR/函数标记。
| 供电或复位 | boot `reset=...`、RTC 记录有效性、串口断点；外部电源/电池电压示波器波形与 ESP32 复位/电源轨同步 | `BROWNOUT` 或电压跌落会加强供电假设；`PANIC/WDT/TASK_WDT` 指向软件/调度故障；`POWERON/EXT/SW` 各自只说明复位类别。单个软件 reset reason 不能证明根因，尤其当前固件禁用了 brownout detector，外部电压采集更关键。
| HTTP 超时 | 主机请求耗时分段、板端 boot uptime/capture id 连续性；handler 自身的 route-specific 指标 | 仅说明请求路径未及时完成。发生在明确上锁之后且随后重启，并不足以证明 HTTP 导致复位；HTTP 导出不应作为唯一保存路径。

## 停止条件和输出

出现任何活动 fault、输出非零但未按流程预期、循环超时显著恶化、诊断数据损坏、重启、异常电压或遥控/上锁不确定，立即结束本轮并保持/恢复 disarmed、四路零输出；不为追数据继续运行。完成后产出只包含 capture、boot/reset、供电和 host request 证据的时间线，并逐条标注“观察”“推断”“未判定”。只有一个捕获同时提供同一 loop sequence 的 IMU wait + scheduler/IPC + reset/power 上下文，才算能回答本轮根因问题。
