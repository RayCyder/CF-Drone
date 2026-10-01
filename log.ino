// Bounded 100 Hz flight recorder. Formatting and transport live outside this path.
#include <esp_timer.h>
#include "flight_log.h"
#include "log_record.h"
#include "diagnostics.h"
#include "control.h"
#include "pid.h"

extern bool armed;
extern bool motorsActive();
extern float batteryVoltage, controlRoll, controlPitch, controlYaw, controlThrottle, controlMode;
extern double controlTime;
extern float motors[4], dt, thrustTarget, motorMixScale;
extern float accelCorrectionConfidence;
extern Vector gyro, acc;
extern int mode;
extern PID rollRatePID, pitchRatePID, yawRatePID;
extern void recordDescentCalibrationSample();

static FlightLogStore flightLog;
static portMUX_TYPE logBufferMux = portMUX_INITIALIZER_UNLOCKED;
static const char *const logColumnNames[FLIGHT_LOG_COLUMNS] = {
    "t", "dt_s", "gyro_x", "gyro_y", "gyro_z", "acc_x", "acc_y", "acc_z",
    "rates.x", "rates.y", "rates.z", "ratesTarget.x", "ratesTarget.y", "ratesTarget.z",
    "attitude.x", "attitude.y", "attitude.z", "attitudeTarget.x", "attitudeTarget.y", "attitudeTarget.z",
    "thrustTarget", "battery_v", "rc_roll", "rc_pitch", "rc_yaw", "rc_throttle", "rc_mode",
    "flight_mode", "rc_age_s", "armed", "fault_mask", "motor_rl", "motor_rr", "motor_fr", "motor_fl",
    "rate_i_x", "rate_i_y", "rate_i_z", "mix_scale", "control_source",
    "accel_correction_confidence"
};
static_assert((DIAG_IMU_INIT | DIAG_IMU_TIMEOUT | DIAG_IMU_INVALID | DIAG_MOTOR_INIT |
    DIAG_RC_LOSS | DIAG_WEB_RC_LOSS | DIAG_BATTERY_LOW | DIAG_LOOP_OVERRUN | DIAG_PARAMETER |
    DIAG_AUTO_TARGET_TIMEOUT | DIAG_INVERTED) <= UINT16_MAX,
    "Expand flight record fault mask when diagnostic bits exceed 16 bits");

int getLogColumnCount() { return FLIGHT_LOG_COLUMNS; }
const char *getLogColumnName(int column) {
    return column >= 0 && column < FLIGHT_LOG_COLUMNS ? logColumnNames[column] : "";
}
FlightLogStatus getFlightLogStatus() {
    portENTER_CRITICAL(&logBufferMux);
    FlightLogStatus result = flightLog.status();
    portEXIT_CRITICAL(&logBufferMux);
    return result;
}
bool freezeFlightLog() {
    if (armed || motorsActive()) return false;
    portENTER_CRITICAL(&logBufferMux);
    bool result = flightLog.freeze();
    portEXIT_CRITICAL(&logBufferMux);
    return result;
}
bool resumeFlightLog() {
    if (armed || motorsActive()) return false;
    portENTER_CRITICAL(&logBufferMux);
    // Preserve the promised post-trigger window even if a maintenance request arrives.
    bool result = flightLog.status().state != POST_TRIGGER;
    if (result) flightLog.resume();
    portEXIT_CRITICAL(&logBufferMux);
    return result;
}
void triggerFlightLog(uint32_t faultMask) {
    const uint64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&logBufferMux);
    flightLog.trigger(faultMask, now);
    portEXIT_CRITICAL(&logBufferMux);
}
bool copyLatestLogRow(float *destination, int capacity, uint32_t *sequence) {
    if (!destination || capacity < FLIGHT_LOG_COLUMNS) return false;
    FlightLogRecord record; uint64_t anchor; uint32_t seq;
    portENTER_CRITICAL(&logBufferMux);
    bool result = flightLog.copyLatest(record, anchor, seq);
    portEXIT_CRITICAL(&logBufferMux);
    if (!result) return false;
    FlightLogCodec::decode(record, anchor, destination, FLIGHT_LOG_COLUMNS);
    if (sequence) *sequence = seq;
    return true;
}
bool copyFrozenLogRow(uint32_t generation, uint32_t row, float *destination, int capacity) {
    if (!destination || capacity < FLIGHT_LOG_LEGACY_COLUMNS) return false;
    FlightLogRecord record; uint64_t anchor;
    portENTER_CRITICAL(&logBufferMux);
    bool result = flightLog.copy(generation, row, record, anchor);
    portEXIT_CRITICAL(&logBufferMux);
    if (!result) return false;
    FlightLogCodec::decode(record, anchor, destination, capacity);
    return true;
}
size_t readFrozenLogBytes(uint32_t generation, uint32_t ofs, uint8_t *destination, size_t maxLen) {
    if (!destination || !maxLen) return 0;
    FlightLogStatus status = getFlightLogStatus();
    if (status.state != FROZEN || status.generation != generation) return 0;
    const uint32_t bytes = status.rowCount * FLIGHT_LOG_LEGACY_ROW_BYTES;
    if (ofs >= bytes) return 0;
    size_t length = maxLen < bytes - ofs ? maxLen : bytes - ofs;
    size_t copied = 0;
    while (copied < length) {
        float row[FLIGHT_LOG_LEGACY_COLUMNS]; uint8_t encoded[FLIGHT_LOG_LEGACY_ROW_BYTES];
        if (!copyFrozenLogRow(generation, ofs / FLIGHT_LOG_LEGACY_ROW_BYTES, row, FLIGHT_LOG_LEGACY_COLUMNS)) return 0;
        FlightLogCodec::legacyBytes(row, encoded);
        const uint32_t within = ofs % FLIGHT_LOG_LEGACY_ROW_BYTES;
        size_t take = FLIGHT_LOG_LEGACY_ROW_BYTES - within;
        if (take > length - copied) take = length - copied;
        memcpy(destination + copied, encoded + within, take);
        copied += take; ofs += take;
    }
    // If a resume raced decoding, callers must discard the whole old-generation chunk.
    status = getFlightLogStatus();
    return status.state == FROZEN && status.generation == generation ? copied : 0;
}
void logData() {
    recordDescentCalibrationSample();
    const uint64_t now = esp_timer_get_time();
    static bool wasArmed = false;
    portENTER_CRITICAL(&logBufferMux);
    if (wasArmed && !armed) flightLog.trigger(FLIGHT_LOG_DISARM_REASON, now);
    wasArmed = armed;
    flightLog.tick(now);
    const bool due = flightLog.sampleDue(now);
    portEXIT_CRITICAL(&logBufferMux);
    if (!due) return;
    const Vector angles = attitude.toEuler(), targetAngles = attitudeTarget.toEuler();
    const float row[FLIGHT_LOG_COLUMNS] = {
        0, dt, gyro.x, gyro.y, gyro.z, acc.x, acc.y, acc.z,
        rates.x, rates.y, rates.z, ratesTarget.x, ratesTarget.y, ratesTarget.z,
        angles.x, angles.y, angles.z, targetAngles.x, targetAngles.y, targetAngles.z,
        thrustTarget, batteryVoltage, controlRoll, controlPitch, controlYaw, controlThrottle, controlMode,
        (float)mode, controlTime > 0 ? (float)(t - controlTime) : -1.0f,
        armed ? 1.0f : 0.0f, (float)getActiveDiagnosticFaults(), motors[0], motors[1], motors[2], motors[3],
        rollRatePID.i * rollRatePID.integral, pitchRatePID.i * pitchRatePID.integral,
        yawRatePID.i * yawRatePID.integral, motorMixScale, (float)getCurrentControlSource(),
        accelCorrectionConfidence
    };
    const FlightLogRecord record = FlightLogCodec::encode(row, now);
    portENTER_CRITICAL(&logBufferMux);
    flightLog.push(record, now);
    portEXIT_CRITICAL(&logBufferMux);
}
