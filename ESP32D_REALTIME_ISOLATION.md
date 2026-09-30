# ESP32-D real-time isolation and partition plan

## Runtime ownership

The Arduino `loopTask` runs on core 1 in the selected ESP32 build (`CONFIG_ARDUINO_RUNNING_CORE=1`). Keep IMU sampling, estimation, RC input arbitration, the control law, motor output, and failsafe decisions on that core.

Core 0 owns network-facing work:

| Task | Core | Priority | Work |
| --- | ---: | ---: | --- |
| `web_rc_http` | 0 | 1 | HTTP handlers and Web RC input enqueueing |
| `wifi_udp_tx` | 0 | 1 | Bounded MAVLink UDP transmit queue |
| `mavlink_rx` | 0 | 1 | UDP receive and byte-level MAVLink parsing; parsed messages enter an 8-item queue |
| `wifi_service` | 0 | 1 | Link/reconnect/captive-DNS maintenance, polled every 20 ms |
| `telemetry_sse` | 0 | 1 | Slow telemetry stream; socket writes stay outside the flight loop |

The flight loop drains at most two parsed MAVLink messages each 200 Hz service pass. Flight commands remain applied by the flight loop, so a communications task cannot directly arm, change mode, or write motor targets. Queue overflow drops parsed messages and is counted. UDP output uses a non-blocking queue and may drop when saturated. No blocking wait is permitted on a flight-loop enqueue.

## Wi-Fi recovery requirements

- `WIFI-REC-1`: A configuration AP is ready only after the ESP32 network event loop reports `AP_START` and the configured SSID and AP address are readable. If the event is absent, the AP is stopped and retried instead of exposing a cached SSID/IP as healthy. An active portal with no associated client is periodically restarted so a stalled beacon path can recover without rebooting the flight controller.
- `WIFI-REC-2`: When saved STA profiles cannot connect within the bounded startup window, provisioning runs in `AP+STA` mode. The STA side continues bounded reconnect attempts while the portal remains available; a recovered STA connection closes the portal and returns to STA-only mode.

Delivery is split into two independently verifiable stages. Stage 1 adds a platform-independent recovery policy and host regressions for AP event confirmation, AP start timeout, no-client refresh, timer wrap, and portal-time STA retry. Stage 2 connects that policy to Arduino Wi-Fi events and the core-0 `wifi_service` task, then verifies the host suite and ESP32-D firmware build. Device acceptance requires forcing the saved network unavailable, observing the provisioning SSID, restoring the network without rebooting, and confirming HTTP recovery at the STA address.

Implementation status (2026-09-30): Stages 1 and 2 are complete. The full host suite passes, the ESP32-D `min_spiffs` image builds at 1,284,687 bytes (65% of its application slot), and the verified image was flashed to the target at 115200 baud after the adapter proved unreliable at 921600 baud. The target rebooted disarmed with no active diagnostic fault and served `/web_rc/status` at its saved STA address. The forced network-loss/AP-beacon/automatic-STA-return scenario remains the required device acceptance check; normal STA boot alone does not prove that recovery path.

## Sensor contracts

`flight_sensor_interfaces.h` defines timestamped barometer and downward-range samples, validity checks, and driver interfaces. No sensor driver, altitude estimator, near-ground controller, or touchdown behavior is enabled by these declarations. Missing, stale, non-finite, or low-quality samples must be rejected by future estimator/control code; existing controlled-descent behavior remains the fallback.

## ESP32-D partition headroom

The current 4 MB default table gives each OTA application slot `0x140000` bytes. The latest default-layout build produced a 1,279,979-byte binary, leaving 30,741 bytes (2.35%) in a slot. That margin is too small for the planned telemetry and sensor-interface growth.

Use the ESP32 `min_spiffs` scheme for ESP32-D builds: two OTA slots of `0x1E0000` bytes each and a `0x20000`-byte SPIFFS partition. The firmware uses only the first two 4 KB SPIFFS sectors for A/B Wi-Fi profile records, so the 128 KB partition remains sufficient. `tools/build_esp32d.sh` selects this scheme explicitly. It preserves OTA while increasing each application slot to 1,966,080 bytes. The latest build is 1,279,995 bytes, leaving 686,085 bytes (34.9%).

The SPIFFS partition moves from offset `0x290000` to `0x3D0000`. Before changing the partition table on a device, flash the default-layout transition firmware and verify `WIFI_PROFILE_BACKUP state=READY` in serial output. The firmware temporarily mirrors saved Wi-Fi profiles to the existing NVS namespace; on first boot with the new table it reads that backup into the new SPIFFS partition, verifies the record, and removes the temporary NVS copy. Do not proceed with a partition-table flash when the backup reports `PENDING` or is absent for a device that has saved Wi-Fi profiles.

## Verification gates

1. Build ESP32-D with `tools/build_esp32d.sh`; record binary size and remaining OTA-slot bytes.
2. On the default partition layout, boot disarmed and verify Wi-Fi profile backup readback before repartitioning.
3. Flash the `min_spiffs` image/table; confirm boot, Wi-Fi profile recovery, IMU/RC initialization, and `armed=0`.
4. With props removed and motors disarmed, compare loop timing at idle, Wi-Fi/Web load, MAVLink input bursts, and telemetry/log downloads. Check P99 bucket, maximum interval, intervals over 1.5 ms, queue drops, and task stack high-water marks.
5. Test link loss, malformed/over-rate MAVLink, queue saturation, and task-creation failure. Confirm the flight loop remains active and existing failsafe/controlled-descent paths are unchanged.

Do not treat a successful compile as proof of control-loop isolation. Device timing evidence is required before flight use. Motor tests are not required for the task-affinity and queue checks; any later low-power motor test must be performed with propellers removed.
