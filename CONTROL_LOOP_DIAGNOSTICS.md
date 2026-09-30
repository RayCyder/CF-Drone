# Control-loop stall localization

Status: a startup scheduler delay is correlated with `ipc1`; the prior long IMU waits remain unexplained. Diagnostic tracing now measures slow SPI-flash cache callbacks and closes pending spans at the next actual loop entry.
Updated: 2026-10-01

## Goal and scope

Identify what occupies the interval when the 1 kHz flight-loop task is absent from the CPU for at least 1.5 ms, and distinguish that from a loop task that remains scheduled but spends time in an ISR or a slow function. This is diagnostic-only work. It does not change attitude estimation, PID, motor output, failsafe, or landing behavior. Test with the aircraft disarmed and propellers removed.

The firmware uses accelerometer and gyroscope inputs only. No GPS, barometer, or magnetometer signals are assumed by this design.

## Evidence and current limits

- `CF-Drone.ino` measures loop and stage durations with `micros()`. These are elapsed wall times and include time when the loop task is preempted.
- The existing 32-entry `LoopOverrunTrace` captures `dt` and stage spans, but not scheduler transitions, task identity, or interrupt duration. A stage label is therefore a location marker, not proof that the stage consumed that much CPU.
- `IMUBase::setupInterruptTimer()` creates a software timer at the configured IMU rate because no data-ready pin is wired. The ISR gives a binary semaphore. This event can be timestamped and its wakeup correlated with task scheduling.
- The inspected build uses Arduino-ESP32 3.3.8-cn, target `esp32` (dual-core), with FreeRTOS task snapshots/runtime statistics enabled. `web_rc_http` and `wifi_udp_tx` are pinned to core 0; `telemetry_sse` is unpinned. The flight-loop core is captured with every event.
- `serviceWiFi()` documents potentially long captive-DNS processing, but a long `wifi_service` stage would be needed to attribute that synchronous call directly. Separate task activity must be checked with scheduler events.

## Requirements and acceptance

| ID | Requirement | Acceptance evidence |
|---|---|---|
| STALL-1 | Correlate a slow loop interval with task switch-out/in events on the loop's core. | A captured interval includes timestamps, task handles, core ID, capture ID and loop sequence; a host test reconstructs the event order. |
| STALL-2 | Avoid recording ordinary short scheduler handoffs. | Handoffs under 1.5 ms do not enter the public ring. |
| STALL-3 | Keep storage bounded and allocation-free. | Fixed-size pending buffer and ring; tests cover overflow, wrap, and overwrite behavior. |
| STALL-4 | Preserve existing flight behavior and keep logging off the hot path. | No serial, network, heap, or NVS calls from trace hooks; firmware diff is diagnostic-only. |
| STALL-5 | Determine whether the flight task was switched out during a long interval; do not mislabel a no-switch interval as a task problem. | A recorded switch interval identifies task handles/core; if no switch explains the delay, report it as ISR/in-task work pending finer tracing. |
| STALL-6 | Verify the observer does not create the failure. | Matched disarmed runs with tracing off/on show trace overhead and overrun-rate delta; no flight/unlock test is part of this acceptance. |

## Chosen data path

1. At each loop entry, increment a 32-bit loop sequence. Keep the current sequence and existing stage timestamps in RAM.
2. Set the active `loopSequence` before the IMU read and advance it consistently with the `dt` sample; update the active sequence after `step()` so later task switches are associated with the next `dt` interval. A FreeRTOS switch-out hook starts a pending capture only when the outgoing task is the flight-loop task. Scheduler switch-in events add task identities, while the next actual flight-loop entry also closes any pending capture; this prevents a missed context-restore hook from leaving a span open across later loop iterations. Each published event carries its loop sequence so it joins the existing `dt` row directly, including the one-loop stage alignment already used by the current trace.
3. Keep the pending capture private until it closes. Publish it only when elapsed time is at least 1,500 us. This avoids writing to the public history for the frequent short `waitForData()` handoffs. Timestamp differences use unsigned 32-bit subtraction and are valid across timer wrap for captures shorter than 2^32 us.
4. Store events in `TaskSwitchTraceRecorder`: production diagnostic-disabled constants remain 64 public events and 24 pending events. The memory-constrained task-trace build records only the flight-loop core and uses 18 public events plus 16 pending events, enough to publish one full pending capture plus an overflow marker. Each event has timestamp, loop sequence, raw task handle, core, kind, capture ID, dropped-event count, and ring sequence (24 bytes on ESP32). No heap is used. Hooks on the other core immediately chain to the real scheduler without touching the recorder.
5. The current implementation records raw 32-bit task handles; `sys` output maps those handles to names outside the hook. A future compact-ID table can replace raw handles if task creation churn or CSV size becomes a problem. The hook must not query task names, format strings, acquire application locks, or call logging/network/NVS APIs. If more than 24 events occur during one captured pause, publish an overflow marker with the omitted-event count.
6. The historical ~6,055 s events have only loop-stage rows, so those records cannot distinguish delayed IMU timer delivery, delayed semaphore wakeup, and time spent reading the sensor. The task-trace diagnostic build now appends a bounded IMU-wait summary to each loop trace: ISR count delta and last ISR timestamp, semaphore take/timeout counts, `read()` attempt/success counts, and total/maximum read duration. The ISR only updates two scalar counters. `imu_interrupt_source` is 0 for no interrupt, 1 for the software timer, and 2 for a DRDY pin; `imu_wait_result` is 1 when data became ready and 0 when the wait returned false (normally a timeout; a DRDY-pin status error can also fail the wait). Timestamps are 32-bit `micros()` values; use unsigned subtraction for short intervals across wrap. No per-tick event rows or wait-semantic changes are introduced.
   These fields exist only in the task-trace build. To fit internal DRAM, its loop-overrun ring holds 20 rows; production keeps the existing 32-row ring and does not compile the ISR/wait instrumentation.
7. Export the trace only while disarmed at `/diag/scheduler.csv`, guarded like `/diag/trace.csv`. Include recorder sequence, capture ID, loop sequence, timestamp, raw task handle, core, event kind, and overwrite/overflow counters. The endpoint disables recording, waits for in-flight hooks to finish, copies the stable ring, and re-enables recording on every exit path. Match handles to task names with the `sys` CLI output, which now includes each task handle.

## Root-cause interpretation

| Observation | Interpretation supported | Follow-up |
|---|---|---|
| Flight task is switched out for ~50 ms; one task owns most of the interval. | Scheduler/task competition is the cause of the loop's absence from CPU. | Identify task core/priority and its blocking/CPU behavior. A task ID alone does not prove which function inside it is responsible. |
| Flight task is blocked in IMU wait and resumes after a late/missing notification. | IMU notification path or scheduler wakeup is implicated. | Use the appended ISR, semaphore, and `read()` summaries to distinguish delayed timer delivery, late task wakeup, and slow sensor reads. |
| No scheduler switch explains the long interval. | The task may have remained current through a long ISR or slow instruction path; task-switch trace alone cannot distinguish these. | Capture ISR/function events in a follow-up SystemView or targeted marker build. |
| A long `imu_wait` row has no matching task-away span. | It may be an IMU notification/read delay, a long ISR, or in-task polling; the stage timer alone cannot choose among them. | Use ISR counter/timestamp and semaphore/read deltas. A missing timer tick implicates timer delivery; a timely tick with delayed take implicates wakeup/scheduling; timely take followed by long read attempts implicates the sensor access/poll path. If these markers do not account for the wall span, capture targeted ISR/function spans. |
| Scheduler trace shows little pause and ISR trace does not explain the span. | Existing stage timer points to a code region but is still insufficient to name the call. | Add temporary nested markers around calls in that stage; do not instrument every function permanently. |

## Ordered implementation stages

### Stage A — recorder contract and host tests

- Requirement IDs: STALL-2, STALL-3.
- Code/test: `task_switch_trace.h`, `tests/test_task_switch_trace.cpp`.
- Tests: short-span discard, long-span event order/task identity, timestamp wrap, core isolation, pending overflow marker, public-ring overwrite and stale-sequence rejection.
- Acceptance: `python3 tests/run_host_tests.py` passes with strict warnings.

### Stage B — target hook and export integration

- Requirement IDs: STALL-1, STALL-4, STALL-5.
- The Arduino package links a precompiled `libfreertos.a`, so sketch-defined FreeRTOS trace macros cannot instrument its context-switch path. The project-owned diagnostic build instead uses GNU ld `--wrap=vTaskSwitchContext`, which the current ESP32 port calls from `portasm.S`; `tools/build_task_trace.sh` applies the wrapper only to the diagnostic build. The normal build has no scheduler wrapper.
- The wrapper records outgoing/incoming task handles around the real scheduler switch on the flight-loop core only. A diagnostic-only wrapper redirects the installed core's `esp_ipc_call_nonblocking()` flash-cache callback to an IRAM wrapper, which measures the callback's own execution duration. `/diag/ipc.csv` retains the longest callback lasting at least 1.5 ms and reports request/start timestamps, duration, caller task and call-site, and target core. This filters frequent short flash handoffs so they cannot overwrite the longest callback. ESP-IDF documents that the SPI flash driver dispatches a cache-suspension callback to the other CPU and holds that CPU until the flash operation completes ([SPI flash API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_flash/index.html)). The private `esp_ipc_call_nonblocking()` declaration comes from the installed ESP32 3.3.8-cn header; the distinct public `esp_ipc_call_blocking()` API waits until its callback completes. The diagnostic build keeps recorder rings and the console queue smaller to fit internal DRAM; default builds retain their original sizes. Hooks use fixed memory, no formatting, sockets, heap, or flash operations. The web endpoints freeze recording and wait for in-flight wrappers before reading the data. CSV serialization has no separate host formatter test yet.
- Acceptance: diagnostic target compiles; a hardware trace must show loop task switch-out/in and known handles; `sys` maps handles to task names. Confirm tracing overhead and no extra >1.5 ms intervals before using the data to attribute a flight issue.

Build the diagnostic image with `tools/build_task_trace.sh esp32:esp32:esp32`. Keep the aircraft disarmed and download `http://<device-address>/diag/scheduler.csv`; match its `task_handle` values to the `Handle` column printed by the `sys` console command. The regular Arduino build does not include the scheduler wrapper.

### Device capture (2026-10-01)

The initial diagnostic image was flashed to the ESP32-D at 115200 baud and verified by esptool hash. The aircraft stayed disarmed with zero throttle, 4.08 V, and no active faults. The scheduler capture is saved as [task-switch-trace-20261001.csv](data/attitude/task-switch-trace-20261001.csv). It records one 52.2 ms interval at loop sequence 40 on core 1: `loopTask` (`0x3ffb8188`) switched out to `ipc1` (`0x3ffb7d60`) at 1,450,275 us, then switched back at 1,502,475 us. The matching loop trace row has `dt_us=53,273`, `control_law_us=52,412`, and `estimate_us=129`. This establishes that this particular control-stage wall-time spike was preemption by `ipc1`; it does not identify which core-0 caller submitted the IPC callback. It is a startup event about 1.5 seconds after boot, so it does not explain the two IMU-wait spikes observed hours into the earlier run.

The `sys` task table mapped the handle to `ipc1`, priority 24 on core 1. A later check at about 146 seconds uptime found no additional scheduler capture. Its newest loop row showed a 6.231 ms `serial_input` span caused by the `sys` command itself; that command-induced sample must not be treated as a normal-load baseline. The live status endpoint still reported `armed=false`, zero throttle, and no faults.

A first follow-up build wrapped `esp_ipc_call_blocking()` (1,300,959 bytes; 124,572 bytes of globals) and was flashed and hash-verified. Its first loop trace row at 1,430 ms uptime was nominal (`dt_us=1,850`, `imu_wait_us=103`); scheduler and IPC exports were empty. This wrapper did not cover the SPI flash cache-suspension path.

A later build wrapped the actual `esp_ipc_call_nonblocking()` flash-cache handoff. On that image, the startup loop row at sequence 39 had `dt_us=56,042`, including `imu_process_us=54,607`; the scheduler trace showed `loopTask` switched to `ipc1` for 54,487 us. The caller record collected just after this event was overwritten 259 times and showed task `0x3ffb8cf0` (`nvs_maintenance` in the `sys` task table), call-site `0x400819c9` inside `spi_flash_disable_interrupts_caches_and_other_cpu()`, and callback `0x40081900` (`spi_flash_op_block_func`). Its request timestamp (1,562,018 us) followed the 1,500,965–1,555,452 us scheduler interval and its IPC dispatch lasted 6 us; therefore it did not prove this was the exact callback responsible for the 54 ms pause.

A ten-minute disarmed monitor on that image had no loop-trace row above `dt_us=1,700`, maximum `imu_wait_us=1,143`, zero faults, and no status errors. The scheduler CSV also exposed pending spans that stayed open for tens of seconds while loop sequence numbers advanced, so those rows were not valid continuous task-away intervals; the loop-entry close path was added afterward.

With the longest-callback retention build, a new disarmed startup capture directly reproduced and identified the stall. Loop sequence 40 had `dt_us=52,255`, including `imu_wait_us=51,001` and `estimate_us=203`. `loopTask` was switched out at 1,341,560 us and resumed at 1,392,463 us (50,903 us); the callback began at 1,341,571 us and lasted 50,882 us. The caller was `nvs_maintenance` (`0x3ffb8cf0`), with call site `0x400819c1` in the SPI-flash cache suspension path, targeting core 1. The callback interval therefore explains essentially all of the `ipc1` exclusion and IMU wait. Captures are saved in [loop-overrun-nvs-reproduced-20261001.csv](data/attitude/loop-overrun-nvs-reproduced-20261001.csv), [task-switch-nvs-reproduced-20261001.csv](data/attitude/task-switch-nvs-reproduced-20261001.csv), and [flash-ipc-nvs-reproduced-20261001.csv](data/attitude/flash-ipc-nvs-reproduced-20261001.csv). Device status before and after capture stayed `armed=false`, throttle zero, 4.08 V; the loop-overrun warning bit cleared by the later status read.

This event is from the existing guarded NVS maintenance path: persistent writes are allowed only when disarmed with motors stopped, and `tryArmWithSystemLog()` rejects arming while a write is active and for ten seconds after a successful batch. It confirms the source of this startup stall and that the affected capture was disarmed; it does not explain the separate IMU-wait spikes seen after roughly 6,055 seconds uptime in earlier logs. The measured estimator span was 203 us, so this event gives no evidence for replacing or retuning the attitude estimator.

The new IMU-wait instrumentation was flashed and its CSV schema verified on the target. A second startup cache callback began during `RC_WEB`: its row had `dt_us=51,596`, `rc_web_us=50,672`, `imu_wait_us=90`, and `estimate_us=102`; the IPC callback lasted 50,585 us and again came from `nvs_maintenance`. The records are saved in [loop-overrun-nvs-rc-web-20261001.csv](data/attitude/loop-overrun-nvs-rc-web-20261001.csv), [task-switch-nvs-rc-web-20261001.csv](data/attitude/task-switch-nvs-rc-web-20261001.csv), and [flash-ipc-nvs-rc-web-20261001.csv](data/attitude/flash-ipc-nvs-rc-web-20261001.csv). That row's adjacent wait telemetry reports source 1 (software timer), one successful semaphore take, one ready sensor read lasting 71 us, and no timeout. Because this long callback occurred during the following loop-body stage, these IMU numbers describe the adjacent wait call; they do not classify the old 6,055-second IMU-wait event.

### Stage C — controlled localization and fix

- Requirement IDs: STALL-5, STALL-6.
- With props removed and motors locked, capture separate matched windows with the Web monitor connected and disconnected. If needed, separately test Wi-Fi disabled; change one condition at a time.
- Keep a baseline with trace hooks disabled. Then enable tracing and compare loop-overrun frequency, maximum delay, and trace event volume. Stop using the tracing build if it materially increases overruns.
- Change only the code/task implicated by the trace. Re-run host tests and firmware compile, then repeat the same locked test window. Do not tune PID or alter landing logic as part of this diagnostic task.
- Acceptance: attribute each representative long interval to a task, ISR, IMU notification, or narrowed function span. The reproduced startup interval is now attributed to the existing guarded NVS flash write; because flash cache suspension is inherent to this persistence operation and arming is interlocked during/after the write, no estimator or control-law change is justified by this disarmed event. Keep the separate long-uptime IMU wait and tracing-overhead comparison open until each has direct evidence.

## Unit-test coverage

`tests/test_task_switch_trace.cpp` tests the pure recorder without FreeRTOS or hardware. Device-level hook correctness cannot be established by host tests; it requires target compilation and a disarmed capture proving that loop task transitions are recorded with the correct core and handle. CSV serialization currently has no independent formatter test; socket behavior requires target verification.

## Known gaps before claiming root cause

- The representative 50.9 ms startup `ipc1`/`imu_wait` interval is now directly attributed to a 50.882 ms SPI flash cache callback from `nvs_maintenance`; it occurred while disarmed, and the existing persistence interlock blocks arming during and after the write.
- The separate 50 ms-class `imu_wait` spikes seen around 6,055 seconds uptime are still unexplained and need a capture from that long-running condition.
- The historical 6,055 s trace did not include scheduler or IMU notification/read markers. The diagnostic firmware now carries the IMU wait telemetry and its target CSV export has been verified, but the old event still needs to recur under the new image before its cause can be determined.
- Scheduler switch-in hooks missed some flight-loop resumptions in earlier captures; a subsequent 50.9 ms device trace closed normally through the wrapped switch-in path. The loop-entry fallback remains host-tested but has not yet been isolated in a target capture where switch-in is intentionally missed.
- This trace records task switches, not ISR entry/exit. A long ISR or long instruction path may appear as the flight task remaining current; if a representative IMU wait is not explained by scheduler events, add timer-ISR/semaphore timestamps or targeted markers around the wait path.
- Matched tracing-off/on runs without CLI output have not been collected, so observer-overhead acceptance is still open. The `sys`-command sample is not a valid baseline for this comparison.
- Host tests and target compilation cannot prove instrumentation overhead or flight behavior; target-only disarmed tests remain mandatory.
