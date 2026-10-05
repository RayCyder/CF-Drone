# 第七轮全局功能复核：Web、仲裁、迫降与验证门禁

基线：参考第二轮总表 `docs/FEATURE_REVIEW_ROUND2.md` 的功能分组，以当前 `HEAD=5c4c4a7` 及工作区未提交改动为审查对象。当前工作区已有 Web/lease/console 相关未提交修改，本轮不刷机、不解锁、不驱动电机；结论只来自源码、测试契约和主机回归。

## 总结

当前工作区已经覆盖上一轮 Web 卡死链路中的核心修复方向：82 端口急停绕过页面 fetch 过滤器、控制请求增加浏览器侧超时、租约过期和被占用开始区分、摇杆发送改为串行合并、状态轮询限制单请求、心跳断连后继续探测。对应证据见 `web_rc_html_controller.h:425-427`, `web_rc_html_controller.h:471-476`, `web_rc_html_controller.h:1282-1298`, `web_rc_html_controller.h:1402-1485`, `web_rc_html_controller.h:1624-1663`, `web_rc_html_controller.h:2090-2095`, `web_rc.ino:1221-1240`。

本轮仍不应发布，原因不是旧问题原样存在，而是验证门禁和几个全局边界已失配。下面按阻塞优先级列出。

## 发现

### P1：主机回归在 MAVLink 迫降接管仲裁处失败

位置：`tests/test_control_state.cpp:422`, `control.ino:240-251`, `mavlink.ino:205-220`, `control.ino:602-620`

影响：`python3 tests/run_host_tests.py` 当前中止在 `test_control_state.cpp:422`。测试期望 controlled landing 期间 `canAcceptMavlinkManualControl()` 为 false，但实现注释与代码允许迫降期间先接受 MAVLink `MANUAL_CONTROL`，再依赖 `landingManualTakeoverRequested()` 的持续输入门槛退出迫降。代码、注释、测试三者对“迫降期间 MAVLink 能否作为人工接管来源”的定义不一致。

验收要求：明确需求后固定一条规则。若允许 MAVLink 接管迫降，应更新测试为“接受输入但只有持续超过 `LANDING_MANUAL_TAKEOVER_HOLD_MS` 才退出迫降”；若不允许，应让 `canAcceptMavlinkManualControl()` 在 controlled landing 下返回 false。之后 `python3 tests/run_host_tests.py` 必须通过。

### P1：Web 调试页面契约测试仍断言旧摇杆发送实现

位置：`tests/test_web_debug_console_ui.js:70-72`, `web_rc_html_controller.h:1282-1298`

影响：`node tests/test_web_debug_console_ui.js` 当前失败。源码已把 `sendJoystickData()` 改为 `joystickRequestPromise` + `joystickSendPending` 的串行合并发送，但测试第 70 行仍匹配旧的 `const request=sendToESP(...)` 形态。这会让后续 Web 修复无法用该测试证明。

验收要求：删除旧实现形态断言，改为验证新不变量：同一时间只有一个摇杆请求在飞，发送循环内复制最新 `currentValues`、更新 `lastSentValues`、递增 `packetStats.sent`，并在 pending 时继续发送最新值。之后 `node tests/test_web_debug_console_ui.js` 必须通过。

### P1：解锁期间仍允许下载完整遥控首页，可能阻塞同一 80 端口控制通道

位置：`web_armed_route_policy.h:8-13`, `web_rc.ino:1622-1629`, `web_rc.ino:1632-1664`

影响：解锁或电机运行时，中间件允许 `GET /`。该 handler 在同一个 `web_rc_http` 任务里分块发送 100 KiB 级 HTML，并且在无进展时最多等 5 秒。82 端口急停已独立，不会被这个请求挡住；但 `/web_rc`、`/web_rc/heartbeat`、`/web_rc/status` 仍在 80 端口同一任务上，慢速或误刷新的首页下载仍可能让当前控制页出现心跳和控制请求延迟。用户已经多次反馈 Web 页面失去响应，这个路径需要实测或继续收窄。

验收要求：飞行中 root 页面请求不得让 `/web_rc/heartbeat` 和 `/web_rc` 超过控制预算。可选修复方向：解锁时拒绝非当前连续性凭证的 `GET /`，或返回极小的恢复页，或把控制 API 与大页面传输彻底隔离。需用慢速首页下载并发心跳的板端或浏览器测试证明。

### P1：页面切后台或锁屏没有主动释放摇杆输入

位置：`web_rc_html_controller.h:1214-1217`, `web_rc_html_controller.h:1265-1272`, `web_rc_html_controller.h:1337-1379`, `web_rc_html_controller.h:2090-2095`, `web_rc.ino:1411-1448`, `web_rc.ino:1453-1463`, `safety.ino:433-450`

影响：前端只在 `pointerup` 和 `pointercancel` 时回中/回悬停，没有 `visibilitychange`、`pagehide` 或 `blur` 处理。若手机锁屏、切 App、系统手势打断页面，但浏览器没有及时发出 pointer cancel，最后一次非零杆量会留在 `currentValues`。后端设计上心跳只刷新 `webRCLastUpdate`，不会刷新 `webRCLastStickUpdate`，所以最终会按摇杆超时进入失联保护；但在超时前，飞控仍可能沿用最后一次摇杆输入。当前 `WEB_RC_LOSS_TIMEOUT_MS` 是 8000 ms，这对“手离开屏幕就回悬停”的产品预期太长。

验收要求：页面隐藏、pagehide、窗口失焦或触控丢失时，应立即清空横滚/俯仰/偏航并把油门回悬停，主动发送一帧摇杆包；发送失败时至少本地停止继续续发旧杆量。需要浏览器自动化或手机实测覆盖“按住摇杆后锁屏/切后台/刷新”的行为。

### P2：Wi-Fi 重启已排队时没有进入解锁门槛

位置：`web_rc.ino:1748-1749`, `web_rc.ino:1782-1784`, `wifi.ino:697-700`, `wifi_recovery_policy.h:77-79`, `control.ino:438-455`

影响：保存 Wi-Fi 或切换 Drone_WiFi 后只调用 `scheduleWiFiRestart()`，实际重启会等到未解锁且电机停止。`armBlockReason()` 没有检查 `wifiRestartScheduled`。操作者若在 1.5 秒窗口或重启被飞行状态延后时解锁，会进入“配置已变更但重启尚未执行”的中间状态；这不是立即坠机路径，但会让链路状态和页面提示变得不可预测。

验收要求：Wi-Fi 重启排队后应禁止解锁，或页面在服务端确认前禁用解锁入口并显示“等待 Wi-Fi 重启”。需要主机测试覆盖 `restartReady()` 与 `armBlockReason()` 的组合。

### P2：Wi-Fi 策略测试仍保留旧“配网门户隔离”模型

位置：`web_rc.ino:1026-1030`, `wifi_recovery_policy.h:57-70`, `tests/test_wifi_recovery_policy.cpp:37-49`

影响：当前实现明确把 `Drone_WiFi` AP 视为正常控制网络，`rejectFlightApiInConfigPortal()` 恒为 false；但 `WifiRecoveryPolicy::flightApiAllowed()` 和 `portalHttpAllowed()` 仍表达旧策略，测试也只验证这些旧函数。结果是测试通过也不能证明当前 AP 模式实际路由策略。

验收要求：删除或更新这组旧策略函数与测试，把测试目标改为当前产品规则：AP 模式允许控制页和控制 API，Wi-Fi 维护操作在解锁或电机运行时拒绝，日志/诊断大下载在飞行中仍被 armed route policy 拦住。

### P2：迫降已接入高度计保护，但能力边界仍是“限幅辅助”，不是稳定自动着陆

位置：`safety.ino:296-377`, `landing_barometer_guard.h:7-24`, `flight_sensor_interfaces.h:43-47`, `tests/test_control_state.cpp:212-229`

影响：迫降路径会读取 `BarometerEstimate`，当估计新鲜、相对高度大于 1 m 且下降速度快于 -0.4 m/s 时，只增加最多 0.025 的推力修正。它不会降低推力、不会判定触地，也不会闭环保持高度。页面文案在 `web_rc_html_controller.h:384-389` 已说明“不是自动着陆”，这与实现一致。

验收要求：实飞前应把验收目标写成“限制过快下降并保留 8 秒硬停机”，而不是“稳定着陆”。若要真正稳定迫降，需要另立需求：高度估计健康度、近地传感器或触地判定、垂直速度闭环、异常高度数据降级策略。

## 已复核为满足当前需求的项目

| 需求 | 当前证据 | 结论 |
| --- | --- | --- |
| 未安装磁力计仍可做加速度计和水平校准 | `calibration_sensor_policy.h:8-19`, `tests/test_calibration_sensor_policy.cpp:5-14`, `web_rc_html_controller.h:302-305`, `web_rc_html_controller.h:402-409` | 满足；磁力计只影响磁航向校准。 |
| 调试页有 `ca` 快捷入口、PID 查询说明和姿态微调提示 | `web_rc_html_controller.h:302-305`, `web_rc_html_controller.h:1915-1918` | 满足；常用命令、六面校准、水平校准和 PID 文案已在调试页。 |
| 横屏小屏不应被快捷工具遮挡日志 | `web_rc_html_controller.h:213-238`, `tests/test_web_debug_console_ui.js:43-49` | 静态满足；仍缺真实手机横屏截图验收。 |
| 起飞前检查显示陀螺、六面加速度计和水平校准 | `control.ino:446-451`, `web_rc_html_controller.h:1682-1692` | 满足；解锁门槛和页面预检一致。 |
| 急停快通道独立于 80 端口 | `web_rc.ino:931-991`, `web_rc.ino:2819-2832`, `web_rc_html_controller.h:1306-1311` | 源码满足；仍需板端端到端延迟实测。 |
| 飞行中禁止无凭证页面抢占 | `web_rc.ino:1787-1821`, `web_rc_lease_policy.h:59-60`, `web_rc_html_controller.h:1433-1457` | 源码满足；刷新连续性依赖同 origin 的 `cfDroneStopToken`。 |

## 验证记录

- `python3 tests/run_host_tests.py`：通过，包括默认、循环监控和 Wi-Fi 启用的控制状态变体。
- `node tests/test_web_debug_console_ui.js`、`node tests/test_web_rc_telemetry_ui.js`、`node tests/test_web_recovery_ui.js`：通过。
- `python3 tests/test_bench_csv_export.py`、`test_boot_motor_self_check_order.py`、`test_preflight_contract.py`、`test_web_rc_http_responsiveness.py`、`test_compare_estimator_replay.py`：通过。
- 完整页和轻量恢复页提取后的 JavaScript 均通过 `node --check`。
- Arduino 默认构建通过：ESP32-D 71% Flash/37% RAM，ESP32-S3 70%/37%，ESP32-C3 41%/21%。
- 未刷写，未执行浏览器真机锁屏/切后台、板端慢下载并发延迟、台架或带桨测试。

## 下一步建议

1. 在设备上验证轻量恢复页与控制/心跳并发，记录慢客户端条件下的最大延迟。
2. 用手机真机覆盖按住摇杆后锁屏、切 App、刷新和 AP/STA 地址切换；确认旧杆量不续发且飞行中其他页面不能抢占。
3. 台架验证 Wi-Fi 保存后立即申请解锁会被拒绝，重启后恢复；确认默认开机电机自检按独立截止归零。
4. 迫降继续按“定推力 + 气压计单向限幅辅助 + 8 秒硬截止”验收；定高、近地和触地另立能力验收。

## 修复结论（2026-10-05）

本文件列出的代码与主机验证缺陷已逐项修复：MAVLink 迫降人工接管契约、Web JS 测试、飞行中轻量首页、页面生命周期释放、Wi-Fi 重启解锁互锁和旧门户策略均已落地。代码层阻塞项已关闭；发布状态仍取决于上述设备、台架和真机浏览器验证，不能由主机测试替代。
