# Full-throttle stutter follow-up

Source: flight-log snapshot exported from the connected flight controller at 2026-10-01. The board was disarmed when exported. Raw data: `full-throttle-stutter-followup-20261001.csv` (400 rows, 427.894–431.924 s).

## Findings

- The snapshot contains 125 samples with RC throttle above 0.8 and a continuous ~1.02 s at full RC throttle. `thrustTarget` is capped at 0.9 in this interval.
- Across those high-throttle samples, `dt_s` is 0.969–1.378 ms; p99 is 1.037 ms. There is no >5 ms main-loop gap synchronized with this interval.
- The single 55.437 ms loop interval occurs at 431.649 s after disarm: throttle, thrust target, and all four motor outputs are zero; `fault_mask=128` records the loop-overrun fault.
- `mix_scale` remains 1.0. Motor commands are not RPM measurements; this log cannot confirm that any motor physically kept its commanded speed, particularly the replaced FR motor.
- In the 125 samples above 0.8 RC throttle, estimated roll averages `-2.84°` while the roll rate target averages `+0.297 rad/s`; pitch averages `-0.15°`. Left motor commands average `0.940` (RL/FL) and right commands `0.826` (RR/FR), so the differential is across the roll axis, not isolated to the replaced FR motor. This is consistent with the controller requesting a roll correction, but the log has no external attitude reference to tell whether that estimate is correct.
- Raw acceleration magnitude averages `11.22 m/s²` (median `11.01 m/s²`), and only `34/125` samples are within `±15%` of 1 g. That indicates substantial non-gravitational acceleration or vibration in the logged interval. Because these are 100 Hz log samples rather than synchronized 1 kHz IMU frames, they do not reveal the exact accelerometer-fusion confidence or true attitude.

## Interpretation

This snapshot does not reproduce a software main-loop stall during the captured high-throttle interval. It does show the attitude controller persistently commanding roll torque while raw acceleration is far from 1 g, but does not establish whether the roll estimate is wrong or whether that commanded differential caused the reported stutter. The delayed 55 ms interval is a separate post-disarm event and must not be treated as evidence of an in-flight stall. Since the log contains no RPM or current data and the load/propeller state for this snapshot is unknown, it cannot distinguish an intermittent motor/ESC/power/mechanical fault from a stutter outside the captured interval. Keep estimator and control gains unchanged until a time-correlated capture includes an attitude reference or synchronized motor RPM/current.
