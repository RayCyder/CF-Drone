# Control-loop stall localization

Status: diagnostic build fits and has been flashed; one startup scheduler delay is localized, while the prior intermittent IMU wait delays and observer-overhead acceptance remain open.
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
2. Set the active `loopSequence` before the IMU read and advance it consistently with the `dt` sample; update the active sequence after `step()` so later task switches are associated with the next `dt` interval. A FreeRTOS switch-out hook starts a pending capture only when the outgoing task is the flight-loop task. A switch-in hook appends the running task handle while that capture is active. Switching back to the flight-loop task closes the capture. Each published event carries this loop sequence so it joins the existing `dt` row directly, including the one-loop stage alignment already used by the current trace.
3. Keep the pending capture private until it closes. Publish it only when elapsed time is at least 1,500 us. This avoids writing to the public history for the frequent short `waitForData()` handoffs. Timestamp differences use unsigned 32-bit subtraction and are valid across timer wrap for captures shorter than 2^32 us.
4. Store events in `TaskSwitchTraceRecorder`: production diagnostic-disabled constants remain 64 public events and 24 pending events. The memory-constrained task-trace build records only the flight-loop core and uses 20 public events plus 16 pending events, enough to publish one full pending capture plus an overflow marker. Each event has timestamp, loop sequence, raw task handle, core, kind, capture ID, dropped-event count, and ring sequence (24 bytes on ESP32). No heap is used. Hooks on the other core immediately chain to the real scheduler without touching the recorder.
5. The current implementation records raw 32-bit task handles; `sys` output maps those handles to names outside the hook. A future compact-ID table can replace raw handles if task creation churn or CSV size becomes a problem. The hook must not query task names, format strings, acquire application locks, or call logging/network/NVS APIs. If more than 24 events occur during one captured pause, publish an overflow marker with the omitted-event count.
6. IMU timer ISR timestamps and semaphore give/take/timeout counters are not yet connected. Add them only if the scheduler trace implicates the IMU wakeup path or leaves the delay unexplained. Keep ISR work to timestamp/counter updates only; do not format or print from an ISR.
7. Export the trace only while disarmed at `/diag/scheduler.csv`, guarded like `/diag/trace.csv`. Include recorder sequence, capture ID, loop sequence, timestamp, raw task handle, core, event kind, and overwrite/overflow counters. The endpoint disables recording, waits for in-flight hooks to finish, copies the stable ring, and re-enables recording on every exit path. Match handles to task names with the `sys` CLI output, which now includes each task handle.

## Root-cause interpretation

| Observation | Interpretation supported | Follow-up |
|---|---|---|
| Flight task is switched out for ~50 ms; one task owns most of the interval. | Scheduler/task competition is the cause of the loop's absence from CPU. | Identify task core/priority and its blocking/CPU behavior. A task ID alone does not prove which function inside it is responsible. |
| Flight task is blocked in IMU wait and resumes after a late/missing notification. | IMU notification path or scheduler wakeup is implicated. | Current trace can identify the wait/task interval; add timer ISR intervals, semaphore give/take counts, and `imu.read()` status in a follow-up capture. |
| No scheduler switch explains the long interval. | The task may have remained current through a long ISR or slow instruction path; task-switch trace alone cannot distinguish these. | Capture ISR/function events in a follow-up SystemView or targeted marker build. |
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
- The wrapper records outgoing/incoming task handles around the real scheduler switch on the flight-loop core only. A second diagnostic wrapper records the longest `esp_ipc_call_blocking()` call targeting that core when its elapsed time is at least 1.5 ms; it stores the caller task, call-site address, callback address, duration, and result in one fixed slot. `/diag/ipc.csv` exports that slot while disarmed. Espressif documents that this API waits for callback completion and that task-context IPC callbacks must not block or yield ([IPC API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ipc.html)). The diagnostic build keeps the recorder ring and console queue smaller to fit internal DRAM; default builds retain their original recorder and output queue sizes. Hooks use fixed memory, no formatting, sockets, heap, or flash operations. The web endpoints freeze recording and wait for in-flight wrappers before reading the data. CSV serialization has no separate host formatter test yet.
- Acceptance: diagnostic target compiles; a hardware trace must show loop task switch-out/in and known handles; `sys` maps handles to task names. Confirm tracing overhead and no extra >1.5 ms intervals before using the data to attribute a flight issue.

Build the diagnostic image with `tools/build_task_trace.sh esp32:esp32:esp32`. Keep the aircraft disarmed and download `http://<device-address>/diag/scheduler.csv`; match its `task_handle` values to the `Handle` column printed by the `sys` console command. The regular Arduino build does not include the scheduler wrapper.

### Device capture (2026-10-01)

The initial diagnostic image was flashed to the ESP32-D at 115200 baud and verified by esptool hash. The aircraft stayed disarmed with zero throttle, 4.08 V, and no active faults. The scheduler capture is saved as [task-switch-trace-20261001.csv](data/attitude/task-switch-trace-20261001.csv). It records one 52.2 ms interval at loop sequence 40 on core 1: `loopTask` (`0x3ffb8188`) switched out to `ipc1` (`0x3ffb7d60`) at 1,450,275 us, then switched back at 1,502,475 us. The matching loop trace row has `dt_us=53,273`, `control_law_us=52,412`, and `estimate_us=129`. This establishes that this particular control-stage wall-time spike was preemption by `ipc1`; it does not identify which core-0 caller submitted the IPC callback. It is a startup event about 1.5 seconds after boot, so it does not explain the two IMU-wait spikes observed hours into the earlier run.

The `sys` task table mapped the handle to `ipc1`, priority 24 on core 1. A later check at about 146 seconds uptime found no additional scheduler capture. Its newest loop row showed a 6.231 ms `serial_input` span caused by the `sys` command itself; that command-induced sample must not be treated as a normal-load baseline. The live status endpoint still reported `armed=false`, zero throttle, and no faults.

A follow-up diagnostic build wraps `esp_ipc_call_blocking()` and was built at 1,300,959 bytes (99% of the 1,310,720-byte app partition; 124,572 bytes of globals, 38% of DRAM). It was flashed and hash-verified. Its first loop trace row at 1,430 ms uptime was nominal (`dt_us=1,850`, `imu_wait_us=103`); `/diag/scheduler.csv` and `/diag/ipc.csv` were empty. The status endpoint returned disarmed, zero throttle, and no faults. The IPC wrapper is now available for a later recurrence, but this brief post-boot sample does not validate the rare-stall fix or the wrapper's effect during a representative long run.

### Stage C — controlled localization and fix

- Requirement IDs: STALL-5, STALL-6.
- With props removed and motors locked, capture separate matched windows with the Web monitor connected and disconnected. If needed, separately test Wi-Fi disabled; change one condition at a time.
- Keep a baseline with trace hooks disabled. Then enable tracing and compare loop-overrun frequency, maximum delay, and trace event volume. Stop using the tracing build if it materially increases overruns.
- Change only the code/task implicated by the trace. Re-run host tests and firmware compile, then repeat the same locked test window. Do not tune PID or alter landing logic as part of this diagnostic task.
- Acceptance: source of a representative long interval is attributable to a task, ISR, IMU notification, or narrowed function span; the chosen fix reduces the matched overrun rate without added hot-path logging or flash writes. If the trace cannot distinguish a source, document the remaining observability gap.

## Unit-test coverage

`tests/test_task_switch_trace.cpp` tests the pure recorder without FreeRTOS or hardware. Device-level hook correctness cannot be established by host tests; it requires target compilation and a disarmed capture proving that loop task transitions are recorded with the correct core and handle. CSV serialization currently has no independent formatter test; socket behavior requires target verification.

## Known gaps before claiming root cause

- One startup control-stage stall is correlated with the core-1 `ipc1` task, but the origin and work of its cross-core callback remain unknown.
- The IPC wrapper has not yet captured a long blocking call; its caller/callback address fields are therefore build-verified but not device-validated against a recurrence.
- The two earlier 50 ms-class `imu_wait` spikes occurred at about 6,055 seconds uptime and were not reproduced during the 146-second diagnostic run. The startup `ipc1` capture is temporally and stage-wise distinct; do not attribute those IMU waits to it.
- This trace records task switches, not ISR entry/exit. A long ISR or long instruction path may appear as the flight task remaining current; if a representative IMU wait is not explained by scheduler events, add timer-ISR/semaphore timestamps or targeted markers around the wait path.
- Matched tracing-off/on runs without CLI output have not been collected, so observer-overhead acceptance is still open. The `sys`-command sample is not a valid baseline for this comparison.
- Host tests and target compilation cannot prove instrumentation overhead or flight behavior; target-only disarmed tests remain mandatory.
