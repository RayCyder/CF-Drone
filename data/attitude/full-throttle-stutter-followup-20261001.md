# Full-throttle stutter follow-up

Source: flight-log snapshot exported from the connected flight controller at 2026-10-01. The board was disarmed when exported. Raw data: `full-throttle-stutter-followup-20261001.csv` (400 rows, 427.894–431.924 s).

## Findings

- The snapshot contains 125 samples with RC throttle above 0.8 and a continuous ~1.02 s at full RC throttle. `thrustTarget` is capped at 0.9 in this interval.
- Across those high-throttle samples, `dt_s` is 0.969–1.378 ms; p99 is 1.037 ms. There is no >5 ms main-loop gap synchronized with this interval.
- The single 55.437 ms loop interval occurs at 431.649 s after disarm: throttle, thrust target, and all four motor outputs are zero; `fault_mask=128` records the loop-overrun fault.
- `mix_scale` remains 1.0. Motor commands are not RPM measurements; this log cannot confirm that any motor physically kept its commanded speed, particularly the replaced FR motor.

## Interpretation

This snapshot does not reproduce a software main-loop stall during the captured high-throttle interval. It does not explain the user's reported full-throttle stutter. The delayed 55 ms interval is a separate post-disarm event and must not be treated as evidence of an in-flight stall. Since the log contains no RPM or current data and the load/propeller state for this snapshot is unknown, it cannot distinguish an intermittent motor/ESC/power/mechanical fault from a stutter outside the captured interval. Keep the estimator and control gains unchanged until a time-correlated capture reproduces the symptom.
