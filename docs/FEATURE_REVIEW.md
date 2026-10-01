# 功能变更倒序审查记录

审查日期：2026-10-01
审查范围：从初始提交 `f550670` 到本记录编写时的 `HEAD`；按非合并提交从新到旧逐项列入提交台账。当前 230 个非合并提交均纳入台账（包含本轮 7 个修复提交）；合并提交只带入已列功能，不重复计数。审查包含当前运行代码、对应主机回归测试、建构/采集工具，以及相关验收记录。

## 功能清单及风险结论

| 功能组 | 覆盖范围 | 风险审查结论 |
| --- | --- | --- |
| Wi-Fi、Web 遥控与输入仲裁 | 配网恢复、Wi-Fi 配置、HTTP/SSE、Web RC 心跳/摇杆新鲜度、物理 RC 与 Web RC 仲裁、解锁原因和状态页 | 已修复：开放配网热点及启动窗口期间拒绝飞行控制、路线、校准、控制台与日志恢复请求；飞行中拒绝 Wi-Fi 扫描/配置修改；推迟配置触发的重启，直到上锁且电机停止。HTTP 状态读取不再刷新/改写诊断状态。 |
| 飞行控制与安全保护 | 解锁门槛、RC 失联、电池低压、迫降/受控下降、模式与 AUTO 目标新鲜度、PID、混控、动作序列、倒置保护 | 复核控制状态迁移和既有主机回归；当前未发现额外可复现缺陷。真实飞行响应仍需硬件验收，主机测试无法替代飞行验证。 |
| 电机与板级配置 | ESP32/C3/S3 引脚、PWM、电机测试、参数热更新 | 已修复：热更新电机引脚前按上一次配置解绑并拉低旧引脚，避免旧 LEDC 绑定残留。 |
| 姿态估计与 IMU | 互补估计、VQF 原型、振动/创新度门控、长 `dt` 限制、陀螺仪偏置学习、温度/电机采样 | 复核门控、无效样本处理、长调度停顿限制和回放数据；主机估计回归通过。VQF 保持可选原型，未替换默认估计器。无新的代码风险留项。 |
| 飞行日志、MAVLink 与系统事件 | 紧凑日志编码、冻结/恢复、CSV/MAVLink 分片、事件持久化迁移、失联/上锁原因 | 检查旧列兼容、记录量化、环形缓冲和分片边界；对应主机回归通过。 |
| 实时性与诊断 | 循环/阶段计时、HTTP 等待限时、任务切换/IPC 追踪、NVS/Flash、ADC 分帧、输出队列 | 阶段预算与整圈卡顿已分开记类（原提交 `9033c0c`）；已修复 IPC 请求重叠时追踪记录错误归属的问题，无法确定时现在报告为未知调用者。 |
| 标定、采集与离线分析工具 | 下降推力/振动标定、IMU 采集、合成真值、参数扫描、温度和日志分析 | 工具参数、边界和确定性输出已复核。已修正 `--gyro-bias` 帮助文字，明确其仅在回放时注入、不写入保留的 IMU CSV。 |
| 文档、实验数据与验收记录 | README、诊断/估计说明、板上采集数据、构建和验收记录 | 不改变运行逻辑；核对其范围及声明，硬件记录均区分了上锁/电机状态和未进行的飞行验证。 |

## 本轮修复

| 提交 | 修复 |
| --- | --- |
| `bb3513e` | Wi-Fi 配置保存计划重启时，等到已上锁且电机停止后才重启；增加策略边界测试。 |
| `065732e` | 澄清估计器扫描工具中的陀螺偏置仅在回放期间附加，不改变留存输入 CSV。 |
| `1aa95dc` | 保存上次电机引脚配置；热重配时先解绑并拉低旧引脚。 |
| `e09c637` | 配网热点处于启动中或已运行时都禁用飞行 API，关闭开放热点短暂暴露控制接口的窗口。 |
| `8741a86` | 并发/排队的 Flash IPC 追踪记录不再借用单槽最新调用者信息误归因；不确定记录标明调用者未知。 |
| `ed323dc` | Web 解锁原因查询不再调用会修改共享诊断状态的刷新逻辑；真正的解锁请求仍在控制路径刷新诊断。 |
| `91a0444` | 配网门户只保留 Wi-Fi 配置操作；拒绝 Web RC、路线、标定、控制台及日志恢复；运行中拒绝 Wi-Fi 扫描、保存和删除。 |

## 验证

- `python3 tests/run_host_tests.py`：全套主机回归通过，包括控制状态、姿态估计、飞行日志、事件存储、动作序列、Wi-Fi 策略和追踪缓冲。
- `python3 -m py_compile tools/*.py`：通过。
- ESP32 生产目标编译通过：1,306,463 B Flash（66%），124,548 B RAM（38%）。
- ESP32-C3 编译通过：527,508 B Flash（26%），71,812 B RAM（21%）；ESP32-S3 编译通过：1,287,848 B Flash（65%），124,100 B RAM（37%）。
- ESP32 IPC-only 诊断目标编译通过：1,309,479 / 1,310,720 B Flash（99%，余 1,241 B），124,532 B RAM（38%）。该余量只涉及可选诊断构建，后续加入诊断代码时需留意。
- USB 枚举发现 `/dev/cu.usbserial-10`。上传命令未能打开端口，沙箱返回 `Operation not permitted`；申请串口权限后又被自动审批拒绝，理由为向飞行器写固件属于有运行安全影响的设备变更。固件未写入。没有执行解锁或电机测试。

## 已知限制

本次检查能证明主机状态机/编码回归和固件编译结果，不能证明实机射频环境、控制响应或带桨飞行行为。USB 固件上传因自动审批拦截未完成。安全相关修复需要在拆桨条件下完成上电、连接、配网和状态接口确认；飞行验证应另行按项目验收流程执行。

## 逐提交审查台账

以下按提交时间倒序列出所有非合并提交。实现类提交归入上方对应功能组；仅文档/实验记录提交不改变运行代码。

| 提交 | 日期 | 变更摘要 | 逐项处置 |
| --- | --- | --- | --- |
| `bb3513e` | 2026-10-01 | Defer Wi-Fi restart until motors stop | 实现/测试/工具；按功能组审查 |
| `065732e` | 2026-10-01 | Clarify replay-only gyro bias sweep option | 实现/测试/工具；按功能组审查 |
| `1aa95dc` | 2026-10-01 | Detach previous motor pins on reconfiguration | 实现/测试/工具；按功能组审查 |
| `321a839` | 2026-10-01 | Record postflash STA outage during USB disconnect preparation | 文档/实验记录；不改运行路径 |
| `e09c637` | 2026-10-01 | Gate flight endpoints while portal starts | 实现/测试/工具；按功能组审查 |
| `8741a86` | 2026-10-01 | Avoid ambiguous flash IPC trace attribution | 实现/测试/工具；按功能组审查 |
| `ed323dc` | 2026-10-01 | Keep Web arm status diagnostics read only | 实现/测试/工具；按功能组审查 |
| `91a0444` | 2026-10-01 | Restrict Wi-Fi setup portal flight controls | 实现/测试/工具；按功能组审查 |
| `ef6732f` | 2026-10-01 | Measure HTTP handler stalls in Web status | 实现/测试/工具；按功能组审查 |
| `aacaeb0` | 2026-10-01 | Record Wi-Fi recovery after controlled reboot | 文档/实验记录；不改运行路径 |
| `89391f8` | 2026-10-01 | Record disarmed STA data-path outage evidence | 文档/实验记录；不改运行路径 |
| `063a5d1` | 2026-10-01 | Guard zero estimator stick gate and restore host regressions | 实现/测试/工具；按功能组审查 |
| `ddbb012` | 2026-10-01 | Add read-only flight link and reboot monitor | 实现/测试/工具；按功能组审查 |
| `a968dff` | 2026-10-01 | Record verified diagnostic flash and low-voltage smoke check | 文档/实验记录；不改运行路径 |
| `9e55378` | 2026-10-01 | Expose link diagnostics and document high-throttle fusion gate | 实现/测试/工具；按功能组审查 |
| `54fc0a1` | 2026-10-01 | Record software disarm cause in flight log trigger | 实现/测试/工具；按功能组审查 |
| `1012158` | 2026-10-01 | Correct battery fault attribution in USB disconnect log | 文档/实验记录；不改运行路径 |
| `bf4cc8d` | 2026-10-01 | Show Web RC arming blockers | 实现/测试/工具；按功能组审查 |
| `e932ee5` | 2026-10-01 | Bound idle HTTP waits and record loop overruns only while armed | 实现/测试/工具；按功能组审查 |
| `4d451d6` | 2026-10-01 | Record armed-loop diagnostic flash and preflight | 文档/实验记录；不改运行路径 |
| `79c2d3e` | 2026-10-01 | Keep physical and Web RC commands from interleaving | 实现/测试/工具；按功能组审查 |
| `8aebe54` | 2026-10-01 | Record Web RC throttle drop and post-disarm loop trace | 文档/实验记录；不改运行路径 |
| `9033c0c` | 2026-10-01 | Distinguish stage budget misses from loop stalls | 实现/测试/工具；按功能组审查 |
| `c5c2f62` | 2026-10-01 | Tune vibration-aware accelerometer fusion defaults | 实现/测试/工具；按功能组审查 |
| `8c8219a` | 2026-10-01 | Record accelerometer correction confidence | 实现/测试/工具；按功能组审查 |
| `15cf4ad` | 2026-10-01 | Analyze high-throttle attitude command imbalance | 文档/实验记录；不改运行路径 |
| `c92ad98` | 2026-10-01 | Record full-throttle stutter follow-up log | 文档/实验记录；不改运行路径 |
| `278a315` | 2026-10-01 | Freeze armed trace only on stall-sized loops | 实现/测试/工具；按功能组审查 |
| `77fae34` | 2026-10-01 | Record repeated FR vibration spectrum comparison | 文档/实验记录；不改运行路径 |
| `589eff9` | 2026-10-01 | Record live loop stall trace recheck | 文档/实验记录；不改运行路径 |
| `b6ed940` | 2026-10-01 | Compare VQF against latest FR vibration capture | 文档/实验记录；不改运行路径 |
| `506ee14` | 2026-10-01 | Record accelerometer fusion filter sweep | 文档/实验记录；不改运行路径 |
| `0bfa7ac` | 2026-10-01 | Capture armed loop stalls before diagnostics overwrite | 实现/测试/工具；按功能组审查 |
| `d31b95e` | 2026-10-01 | Record post-calibration idle trace check | 文档/实验记录；不改运行路径 |
| `ea9f315` | 2026-10-01 | Prevent learned gyro bias from absorbing slow turns | 实现/测试/工具；按功能组审查 |
| `03cf70a` | 2026-10-01 | Expose accelerometer fusion filter tuning | 实现/测试/工具；按功能组审查 |
| `b5f5075` | 2026-10-01 | Make estimator parameter sweeps reproducible | 实现/测试/工具；按功能组审查 |
| `6537c0e` | 2026-10-01 | Tighten adaptive attitude correction threshold | 实现/测试/工具；按功能组审查 |
| `3936cca` | 2026-10-01 | Capture IMU semaphore wait latency in trace | 实现/测试/工具；按功能组审查 |
| `f99e13f` | 2026-10-01 | Record production loop stall spot check | 文档/实验记录；不改运行路径 |
| `de960eb` | 2026-10-01 | Make estimator innovation threshold replayable | 实现/测试/工具；按功能组审查 |
| `72873bf` | 2026-10-01 | Record innovation threshold noise scan | 文档/实验记录；不改运行路径 |
| `069afea` | 2026-10-01 | Record combined trace target observation | 文档/实验记录；不改运行路径 |
| `12ff26d` | 2026-10-01 | Note full trace build validation | 文档/实验记录；不改运行路径 |
| `9f9c693` | 2026-10-01 | Record isolated trace hook validation | 文档/实验记录；不改运行路径 |
| `8c396c4` | 2026-10-01 | Add isolated task trace build modes | 实现/测试/工具；按功能组审查 |
| `d9b46e8` | 2026-10-01 | Record controlled NVS stall reproduction attempt | 文档/实验记录；不改运行路径 |
| `c2b8fc1` | 2026-10-01 | Correlate IMU stalls with NVS persistence timing | 文档/实验记录；不改运行路径 |
| `35e672a` | 2026-10-01 | Record stationary gyro bias validation | 文档/实验记录；不改运行路径 |
| `502bf1b` | 2026-10-01 | Correlate historical IMU stalls with flight telemetry | 文档/实验记录；不改运行路径 |
| `5b5d1b6` | 2026-10-01 | Adapt accelerometer gain to gravity innovation | 实现/测试/工具；按功能组审查 |
| `c7766c6` | 2026-10-01 | Add truth-based estimator replay scenarios | 实现/测试/工具；按功能组审查 |
| `1669106` | 2026-10-01 | Record estimator stall guard board validation | 文档/实验记录；不改运行路径 |
| `0b57650` | 2026-10-01 | Bound accelerometer correction after loop stalls | 实现/测试/工具；按功能组审查 |
| `102d387` | 2026-10-01 | Test estimator update after long loop stalls | 实现/测试/工具；按功能组审查 |
| `778a163` | 2026-10-01 | Record board validation of long-dt filter fix | 文档/实验记录；不改运行路径 |
| `ae60db4` | 2026-10-01 | Clamp variable LPF gain after long stalls | 实现/测试/工具；按功能组审查 |
| `aca6098` | 2026-10-01 | Compare estimator parameter sweep across motor captures | 文档/实验记录；不改运行路径 |
| `ab54e81` | 2026-10-01 | Document cross-core flash loop stall evidence | 文档/实验记录；不改运行路径 |
| `5930ca1` | 2026-10-01 | Reduce loop impact of serial preflight diagnostics | 实现/测试/工具；按功能组审查 |
| `4354740` | 2026-10-01 | Summarize estimator replay across 18 motor captures | 文档/实验记录；不改运行路径 |
| `e2768fd` | 2026-10-01 | Preserve worst loop stall trace and capture motor data | 实现/测试/工具；按功能组审查 |
| `31ee7bc` | 2026-10-01 | Capture estimator candidate motor A/B data | 文档/实验记录；不改运行路径 |
| `e415a8d` | 2026-10-01 | Add replay-tested vibration tolerance candidate | 实现/测试/工具；按功能组审查 |
| `c8227db` | 2026-10-01 | Include post-replacement RR in estimator replay | 实现/测试/工具；按功能组审查 |
| `f11e8c9` | 2026-10-01 | Capture post-replacement RR motor IMU data | 文档/实验记录；不改运行路径 |
| `be6df8e` | 2026-10-01 | Record sampled scheduler hook cost | 文档/实验记录；不改运行路径 |
| `ede67ac` | 2026-10-01 | Record estimator replay on temperature captures | 文档/实验记录；不改运行路径 |
| `c935599` | 2026-10-01 | Collect repeated raw gyro temperature samples | 实现/测试/工具；按功能组审查 |
| `bacc70c` | 2026-10-01 | Capture raw gyro data for temperature analysis | 实现/测试/工具；按功能组审查 |
| `44e3d27` | 2026-10-01 | Record latest estimator and loop diagnostics | 文档/实验记录；不改运行路径 |
| `7998241` | 2026-10-01 | Add static IMU temperature captures | 文档/实验记录；不改运行路径 |
| `10e4fae` | 2026-10-01 | Flush boot NVS before control loop | 实现/测试/工具；按功能组审查 |
| `01052a0` | 2026-10-01 | Bound IMU capture export work per loop | 实现/测试/工具；按功能组审查 |
| `03c87a6` | 2026-10-01 | Clarify IMU timer diagnostics | 实现/测试/工具；按功能组审查 |
| `476d850` | 2026-10-01 | Record task trace overhead comparison | 文档/实验记录；不改运行路径 |
| `960f638` | 2026-10-01 | Record long-run historical stall window retest | 文档/实验记录；不改运行路径 |
| `0f34407` | 2026-10-01 | Expose VQF confidence gate replay scan | 实现/测试/工具；按功能组审查 |
| `c0eed9f` | 2026-10-01 | Record VQF confidence gate replay results | 文档/实验记录；不改运行路径 |
| `5033b77` | 2026-10-01 | Make VQF time constant replay configurable | 实现/测试/工具；按功能组审查 |
| `6d1f896` | 2026-10-01 | Record long-run control-loop diagnostic sample | 文档/实验记录；不改运行路径 |
| `5fb2ad9` | 2026-10-01 | Document VQF time constant sensitivity results | 文档/实验记录；不改运行路径 |
| `a29fa84` | 2026-10-01 | Trace IMU wait timing for long-run stalls | 实现/测试/工具；按功能组审查 |
| `32fcc98` | 2026-10-01 | Attribute startup loop stall to NVS flash callback | 文档/实验记录；不改运行路径 |
| `a93e1f2` | 2026-10-01 | Capture long flash callbacks and close loop trace spans | 实现/测试/工具；按功能组审查 |
| `bb008d8` | 2026-10-01 | Trace SPI flash IPC during control loop stalls | 实现/测试/工具；按功能组审查 |
| `665790a` | 2026-10-01 | Trace slow blocking IPC calls in diagnostics | 实现/测试/工具；按功能组审查 |
| `e816cf1` | 2026-10-01 | Fit scheduler trace into ESP32 diagnostic build | 实现/测试/工具；按功能组审查 |
| `74f1497` | 2026-10-01 | Bound console output and gate gyro bias updates | 实现/测试/工具；按功能组审查 |
| `2317819` | 2026-10-01 | Add repeatable motor IMU capture tool | 实现/测试/工具；按功能组审查 |
| `c9f6130` | 2026-10-01 | Fix incremental gyro bias calibration | 实现/测试/工具；按功能组审查 |
| `0ae97c4` | 2026-10-01 | Add web motor vibration calibration | 实现/测试/工具；按功能组审查 |
| `67fd071` | 2026-10-01 | Capture armed throttle and VQF replay evidence | 实现/测试/工具；按功能组审查 |
| `4389ff3` | 2026-10-01 | Compare VQF replay on replaced motor captures | 文档/实验记录；不改运行路径 |
| `7b87aa7` | 2026-10-01 | Add repeat captures for replaced front-right motor | 文档/实验记录；不改运行路径 |
| `b5a1a6d` | 2026-10-01 | Record post-replacement motor IMU captures | 实现/测试/工具；按功能组审查 |
| `60ba95e` | 2026-10-01 | Add accelerometer gain replay sweeps | 实现/测试/工具；按功能组审查 |
| `2421985` | 2026-09-30 | Add dynamic truth regression for vibration gating | 实现/测试/工具；按功能组审查 |
| `ba12f2f` | 2026-09-30 | Document multirotor throttle capture metrics | 文档/实验记录；不改运行路径 |
| `2e35c56` | 2026-09-30 | Record short four-motor estimator capture | 文档/实验记录；不改运行路径 |
| `ca9a9d1` | 2026-09-30 | Improve estimator truth replay and threshold sweep | 实现/测试/工具；按功能组审查 |
| `0194ded` | 2026-09-30 | Add same-input estimator replay comparison | 实现/测试/工具；按功能组审查 |
| `1d82142` | 2026-09-30 | Add baseline estimator repeat captures | 文档/实验记录；不改运行路径 |
| `f00a628` | 2026-09-30 | Add repeat estimator bench capture | 文档/实验记录；不改运行路径 |
| `d2b9aa1` | 2026-09-30 | Record prop-off estimator bench comparison | 文档/实验记录；不改运行路径 |
| `cadf48b` | 2026-09-30 | Add vibration-aware accelerometer confidence | 实现/测试/工具；按功能组审查 |
| `65e9e5a` | 2026-09-30 | Guard VQF fusion against invalid accelerometer samples | 实现/测试/工具；按功能组审查 |
| `5525907` | 2026-09-30 | Add optional VQF attitude prototype | 实现/测试/工具；按功能组审查 |
| `4436140` | 2026-09-30 | Record disarmed motor attitude captures | 文档/实验记录；不改运行路径 |
| `3018612` | 2026-09-30 | Keep disarmed motor test at full IMU sample rate | 实现/测试/工具；按功能组审查 |
| `b3b7a05` | 2026-09-30 | Recover Wi-Fi provisioning without reboot | 实现/测试/工具；按功能组审查 |
| `88c19d9` | 2026-09-30 | Record RC UART receive health snapshot | 文档/实验记录；不改运行路径 |
| `348647b` | 2026-09-30 | Clarify RC input freshness diagnostic | 文档/实验记录；不改运行路径 |
| `5f3f26c` | 2026-09-30 | Expose RC receive parser health | 实现/测试/工具；按功能组审查 |
| `8188b3c` | 2026-09-30 | Record repeat static estimator replay | 文档/实验记录；不改运行路径 |
| `e37397e` | 2026-09-30 | Add repeat static IMU dataset | 文档/实验记录；不改运行路径 |
| `eecca43` | 2026-09-30 | Add high-rate IMU capture for estimator evaluation | 实现/测试/工具；按功能组审查 |
| `0b60d2d` | 2026-09-30 | Improve attitude fusion and control diagnostics | 实现/测试/工具；按功能组审查 |
| `79bf7ac` | 2026-09-30 | Fix root page PROGMEM response | 实现/测试/工具；按功能组审查 |
| `0b701e8` | 2026-09-30 | Add ESP32-D acceptance test plan | 文档/实验记录；不改运行路径 |
| `757a602` | 2026-09-30 | Isolate ESP32-D flight and communications | 实现/测试/工具；按功能组审查 |
| `72b033f` | 2026-09-30 | docs: correct loop trace buffer size | 文档/实验记录；不改运行路径 |
| `03cb6cd` | 2026-09-29 | Explain locked LED alerts in Web diagnostics | 实现/测试/工具；按功能组审查 |
| `178d74f` | 2026-09-29 | Add flight-loop stall diagnostics | 实现/测试/工具；按功能组审查 |
| `0ae1c6d` | 2026-09-28 | Rate limit MAVLink work in control loop | 实现/测试/工具；按功能组审查 |
| `adc0323` | 2026-09-28 | Skip captive DNS maintenance while armed | 实现/测试/工具；按功能组审查 |
| `8c1a96c` | 2026-09-28 | Reduce periodic IMU wait overruns | 实现/测试/工具；按功能组审查 |
| `3a1fb5c` | 2026-09-28 | Spread MAVLink telemetry across loop ticks | 实现/测试/工具；按功能组审查 |
| `94b5ff7` | 2026-09-28 | Skip idle parameter scans and split IMU timing | 实现/测试/工具；按功能组审查 |
| `465e84e` | 2026-09-28 | Queue MAVLink UDP sends off control loop | 实现/测试/工具；按功能组审查 |
| `74bc2c4` | 2026-09-28 | Remove landing confirmation prompt | 实现/测试/工具；按功能组审查 |
| `3687883` | 2026-09-28 | Rate limit MAVLink bulk output | 实现/测试/工具；按功能组审查 |
| `0bc20f9` | 2026-09-28 | Record final on-device timing follow-up | 文档/实验记录；不改运行路径 |
| `608e71b` | 2026-09-28 | Record six-axis firmware validation results | 文档/实验记录；不改运行路径 |
| `5e07beb` | 2026-09-28 | Bound ADC work and make estimator filters dt-aware | 实现/测试/工具；按功能组审查 |
| `e7a7f17` | 2026-09-28 | Add bounded onboard open-loop action sequences | 实现/测试/工具；按功能组审查 |
| `876d085` | 2026-09-28 | Measure millisecond loop overruns and preserve event boot identity | 实现/测试/工具；按功能组审查 |
| `f2f8951` | 2026-09-28 | Preserve compact flight snapshots and bound log exports | 实现/测试/工具；按功能组审查 |
| `3074356` | 2026-09-28 | Fix control ownership and persistent failsafe descent | 实现/测试/工具；按功能组审查 |
| `d51653e` | 2026-09-28 | Fix monotonic flight timing and bounded PID integration | 实现/测试/工具；按功能组审查 |
| `85035ce` | 2026-09-28 | Improve diagnostics and controlled descent | 实现/测试/工具；按功能组审查 |
| `03b6626` | 2026-08-31 | 未完成ESP32-C3适配 | 文档/实验记录；不改运行路径 |
| `08775bb` | 2026-07-18 | 修复参数存储与遥控相关的若干稳定性问题 | 实现/测试/工具；按功能组审查 |
| `df86a68` | 2026-07-18 | 更新LICENSE为CF-Drone附加许可条款v1.1 | 实现/测试/工具；按功能组审查 |
| `890a674` | 2026-06-24 | 整理板级配置 | 实现/测试/工具；按功能组审查 |
| `08de673` | 2026-06-24 | 优化性能，规范板型配置 | 实现/测试/工具；按功能组审查 |
| `2d822b6` | 2026-06-24 | 优化控制台提示及Web遥控器初始化输出 | 实现/测试/工具；按功能组审查 |
| `1ed4d4c` | 2026-06-18 | 网页遥控警告提示优化 | 实现/测试/工具；按功能组审查 |
| `6714554` | 2026-06-18 | 优化解锁限制判断顺序 | 实现/测试/工具；按功能组审查 |
| `7faba4c` | 2026-06-18 | 优化遥控消息提示 | 实现/测试/工具；按功能组审查 |
| `bae82e7` | 2026-06-17 | 优化IMU故障处理 | 实现/测试/工具；按功能组审查 |
| `ea932c0` | 2026-06-15 | 针对esp32-c3的性能优化 | 实现/测试/工具；按功能组审查 |
| `46e4955` | 2026-06-10 | 加入IMU超时机制，不阻塞主循环 | 实现/测试/工具；按功能组审查 |
| `9d6dd0f` | 2026-06-10 | WIFI禁用省电模式前置 | 实现/测试/工具；按功能组审查 |
| `08694ba` | 2026-06-10 | 针对ESP32-C3的优化 | 实现/测试/工具；按功能组审查 |
| `1f77e7c` | 2026-06-07 | 优化迫降策略 | 实现/测试/工具；按功能组审查 |
| `24de3cb` | 2026-06-07 | 禁用姿态水平修正，因为有机械偏差时反而会起负作用；优化软件配平 | 实现/测试/工具；按功能组审查 |
| `b2fd5fe` | 2026-06-04 | 参数添加注释 | 实现/测试/工具；按功能组审查 |
| `e810277` | 2026-06-04 | 减少外部依赖 | 实现/测试/工具；按功能组审查 |
| `8d443ec` | 2026-06-04 | 支持ELRS/CRSF遥控协议 | 实现/测试/工具；按功能组审查 |
| `071f709` | 2026-06-04 | 测试PID | 实现/测试/工具；按功能组审查 |
| `ee01a10` | 2026-06-01 | 增加软件配平和水平修正摇杆感知，分别优化机械偏差造成的漂移和飞行中水平修正造成的过度修正 | 实现/测试/工具；按功能组审查 |
| `8febece` | 2026-06-01 | 缩短飞行日志到8秒，节省DRAM开销 | 实现/测试/工具；按功能组审查 |
| `2aba077` | 2026-05-30 | 网页遥控地址去掉端口，兼容旧地址会自动跳转 | 实现/测试/工具；按功能组审查 |
| `2d92528` | 2026-05-30 | ESP32C3支持指示灯 | 实现/测试/工具；按功能组审查 |
| `effc7c9` | 2026-05-30 | 优化电机去饱和逻辑，测试50mm轴距PID | 实现/测试/工具；按功能组审查 |
| `a175a8e` | 2026-05-27 | 适当调整默认PID | 实现/测试/工具；按功能组审查 |
| `a3adb8b` | 2026-05-25 | 暂时禁用姿态水平修正，调整默认PID | 实现/测试/工具；按功能组审查 |
| `742f66f` | 2026-05-22 | 修正由速率改为倾角判断 | 实现/测试/工具；按功能组审查 |
| `445a14a` | 2026-05-22 | 姿态自动校正增加自适应权重 | 实现/测试/工具；按功能组审查 |
| `105d289` | 2026-05-21 | 同步更新姿态控制优化 | 实现/测试/工具；按功能组审查 |
| `09d7345` | 2026-05-21 | 电机测试时的功率改为30% | 实现/测试/工具；按功能组审查 |
| `3af5bc2` | 2026-05-20 | 实物适配ESP32-C3 | 实现/测试/工具；按功能组审查 |
| `c6b5d59` | 2026-05-16 | 修改SBUS接收机默认GPIO | 实现/测试/工具；按功能组审查 |
| `b2e312b` | 2026-05-16 | 优化电量检测精度和低电保护阈值 | 实现/测试/工具；按功能组审查 |
| `2e04f1d` | 2026-05-07 | 更新文档 | 文档/实验记录；不改运行路径 |
| `e78fe63` | 2026-05-07 | 优化校准提示 | 实现/测试/工具；按功能组审查 |
| `ae0c081` | 2026-05-07 | 去掉一个网页遥控控制台无用的心跳输出 | 实现/测试/工具；按功能组审查 |
| `cb55065` | 2026-05-03 | 增加姿态传感器后的提示 | 实现/测试/工具；按功能组审查 |
| `7578f21` | 2026-05-03 | 重构低电保护逻辑 | 实现/测试/工具；按功能组审查 |
| `537b622` | 2026-05-03 | 优化电量低报警和动作逻辑 | 实现/测试/工具；按功能组审查 |
| `c3c5266` | 2026-05-03 | 修正死区判断 | 实现/测试/工具；按功能组审查 |
| `632de13` | 2026-05-03 | 修复死区怠速 | 实现/测试/工具；按功能组审查 |
| `10b49f0` | 2026-05-03 | 修正怠速判断 | 实现/测试/工具；按功能组审查 |
| `c9f052e` | 2026-05-01 | 禁用WIFI省电模式 | 实现/测试/工具；按功能组审查 |
| `942f100` | 2026-05-01 | 缩小摇杆死区 | 实现/测试/工具；按功能组审查 |
| `af85a07` | 2026-04-30 | 更新文档 | 文档/实验记录；不改运行路径 |
| `f372f9c` | 2026-04-30 | 减小摇杆死区 | 实现/测试/工具；按功能组审查 |
| `d98ae9a` | 2026-04-30 | 调整网页遥控排版细节 | 实现/测试/工具；按功能组审查 |
| `f7330a0` | 2026-04-30 | 文档更新 | 文档/实验记录；不改运行路径 |
| `22e735a` | 2026-04-30 | 油门逻辑优化，增加MOT_THR_MIN和MOT_THR_MAX用于控制动力上下限，默认0.1/1.0 | 实现/测试/工具；按功能组审查 |
| `6e385fe` | 2026-04-30 | 优化电量报警逻辑，低电禁止解锁、自动上锁、自动降落 | 实现/测试/工具；按功能组审查 |
| `d6503b9` | 2026-04-28 | 统一mavlink的加速度计数据单位 | 实现/测试/工具；按功能组审查 |
| `6e32416` | 2026-04-28 | 修复BUG | 实现/测试/工具；按功能组审查 |
| `148b5b7` | 2026-04-28 | 适配ESP32-S3 | 实现/测试/工具；按功能组审查 |
| `d0665b9` | 2026-04-25 | 修复一处倒置保护的BUG，并增大触发阈值 | 实现/测试/工具；按功能组审查 |
| `c4907b2` | 2026-04-24 | 怠速油门参数化，使用命令p MOT_IDLE_THRUST 0.28设置，默认0.1 | 实现/测试/工具；按功能组审查 |
| `45509d5` | 2026-04-22 | 优化ESP32-C3 SPI引脚定义 | 实现/测试/工具；按功能组审查 |
| `9222e48` | 2026-04-20 | 适配ESP32-C3 | 实现/测试/工具；按功能组审查 |
| `b88d6ef` | 2026-04-19 | 修改默认SBUS引脚 | 实现/测试/工具；按功能组审查 |
| `5979e52` | 2026-04-17 | 修复字符错误 | 实现/测试/工具；按功能组审查 |
| `6c376f1` | 2026-04-17 | 更新文档 | 文档/实验记录；不改运行路径 |
| `a91aad8` | 2026-04-17 | 修改调试命令显示文字 | 实现/测试/工具；按功能组审查 |
| `d985ada` | 2026-04-17 | 修复网页遥控调试日志显示过长被阶段的问题 | 实现/测试/工具；按功能组审查 |
| `4436ee0` | 2026-04-17 | 更新开源协议、说明文档，遥控页面内容 | 实现/测试/工具；按功能组审查 |
| `f5f4b02` | 2026-04-16 | 修改低电警告阈值 | 实现/测试/工具；按功能组审查 |
| `751abfb` | 2026-04-16 | 调整调试日志打印相关 | 实现/测试/工具；按功能组审查 |
| `14839d6` | 2026-04-16 | 更新文档 | 文档/实验记录；不改运行路径 |
| `dc4bec2` | 2026-04-16 | 修改低电压警告阈值 | 实现/测试/工具；按功能组审查 |
| `d94e8d6` | 2026-04-15 | 文档更新 | 文档/实验记录；不改运行路径 |
| `0fd5514` | 2026-04-15 | 增加状态指示灯 | 实现/测试/工具；按功能组审查 |
| `03daafa` | 2026-04-15 | 适当提高网页遥控发包频率 | 实现/测试/工具；按功能组审查 |
| `d180b7a` | 2026-04-15 | 修复网络延迟计算的逻辑错误 | 实现/测试/工具；按功能组审查 |
| `d5929ac` | 2026-04-15 | 常规功能更新，WIFI账号密码、电机引脚参数化，可后期通过控制台设置 | 实现/测试/工具；按功能组审查 |
| `f0c988c` | 2026-04-09 | 修改注释 | 实现/测试/工具；按功能组审查 |
| `00d658d` | 2026-03-31 | 更新文档 | 文档/实验记录；不改运行路径 |
| `afa8f29` | 2026-03-30 | 优化调试回显 | 实现/测试/工具；按功能组审查 |
| `6bd093f` | 2026-03-30 | 修复部分调试命令回显失败 | 实现/测试/工具；按功能组审查 |
| `26b9ade` | 2026-03-30 | 修正调试显示 | 实现/测试/工具；按功能组审查 |
| `164a869` | 2026-03-30 | 优化WEB遥控调试功能 | 实现/测试/工具；按功能组审查 |
| `1269f72` | 2026-03-29 | 修复BUG | 实现/测试/工具；按功能组审查 |
| `7871a21` | 2026-03-29 | 修复BUG | 实现/测试/工具；按功能组审查 |
| `d5f57d0` | 2026-03-28 | 清理WEB遥控服务端冗余 | 实现/测试/工具；按功能组审查 |
| `4127f6d` | 2026-03-28 | 重构WEB遥控底层 | 实现/测试/工具；按功能组审查 |
| `b50112a` | 2026-03-28 | 大幅优化WEB遥控器性能 | 实现/测试/工具；按功能组审查 |
| `a0c1cfc` | 2026-03-27 | 修改文档 | 文档/实验记录；不改运行路径 |
| `cfd10b3` | 2026-03-27 | 大幅重构功能 | 实现/测试/工具；按功能组审查 |
| `343868d` | 2026-03-27 | Add GNU GPL v3 license | 文档/实验记录；不改运行路径 |
| `afc723e` | 2026-03-27 | 文档 | 文档/实验记录；不改运行路径 |
| `f550670` | 2026-03-27 | frist | 实现/测试/工具；按功能组审查 |
