#include "vertical_flight.h"

#include "external_sensors.h"
#include "flight_sensor_interfaces.h"
#include "navigation_origin.h"
#include "optical_flow_calibration.h"
#include "range_reference.h"
#include "quaternion.h"
#include "vertical_navigation.h"
#include "vector.h"

extern Quaternion attitude;
extern Vector acc;
extern Vector gyro;
extern float dt;
extern bool armed;

namespace {
VerticalNavigationEstimator verticalEstimator;
VerticalPositionController verticalController;
VerticalFlightState verticalSnapshot;
portMUX_TYPE verticalFlightMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t lastUpdateUs = 0;
uint32_t lastFlowTimestampUs = 0;
RangeReferenceTracker rangeReference;
OpticalFlowCalibrationAccumulator flowCalibration;
NavigationOriginState flowOrigin;
float routeAltitudeMeters = 0.0f;
float routeHeadingRadians = 0.0f;
bool routeTargetValid = false;
bool altitudeControllerActive = false;
float altitudeTargetMeters = 0.0f;
float verticalSpeedTargetMps = 0.0f;
float lastVerticalThrust = 0.0f;

constexpr uint32_t SENSOR_MAX_AGE_US = 250000U;
constexpr uint8_t FLOW_MIN_QUALITY = 20;
constexpr float RANGE_MIN_METERS = 0.04f;
constexpr float RANGE_MAX_METERS = 3.5f;
constexpr float FLOW_MAX_TILT_RAD = 0.5235988f;

float wrapRadians(float angle) {
	while (angle > PI) angle -= 2.0f * PI;
	while (angle < -PI) angle += 2.0f * PI;
	return angle;
}

void updateFlowShadow(const OpticalFlowSample &flow, const DownwardRangeSample &range,
	uint32_t nowUs, float roll, float pitch, float yaw, VerticalFlightState &next) {
	const bool rangeScaleUsable = downwardRangeSampleUsable(range, nowUs, SENSOR_MAX_AGE_US, 1) &&
		range.distanceMeters >= RANGE_MIN_METERS && range.distanceMeters <= RANGE_MAX_METERS &&
		fabsf(roll) <= FLOW_MAX_TILT_RAD && fabsf(pitch) <= FLOW_MAX_TILT_RAD;
	const bool flowUsable = opticalFlowSampleUsable(flow, nowUs, 100000U, FLOW_MIN_QUALITY) &&
		rangeScaleUsable && flow.timestampUs != lastFlowTimestampUs;
	flowCalibration.observe(flow.timestampUs, flow.valid, flow.motionDetected,
		flow.deltaX, flow.deltaY, flow.deltaXAngularRadians, flow.deltaYAngularRadians,
		flow.quality, gyro.x, gyro.y, next.rangeAglMeters, rangeScaleUsable, flowUsable);
	if (!flowUsable) {
		next.flowValid = false;
		flowOrigin.vxMps *= 0.95f;
		flowOrigin.vyMps *= 0.95f;
		return;
	}
	const float sampleDt = lastFlowTimestampUs ?
		(float)(uint32_t)(flow.timestampUs - lastFlowTimestampUs) * 1e-6f : 0.01f;
	lastFlowTimestampUs = flow.timestampUs;
	if (sampleDt < 0.004f || sampleDt > 0.05f) return;

	// PMW3901 reports angular image displacement. Remove the matching body-rate
	// rotation before multiplying by AGL. Axis signs remain diagnostic until the
	// expansion-board mounting transform is verified by the prop-off hand test.
	const float bodyVX = flow.motionDetected ?
		-(flow.deltaYAngularRadians - gyro.x * sampleDt) * next.rangeAglMeters / sampleDt : 0.0f;
	const float bodyVY = flow.motionDetected ?
		 (flow.deltaXAngularRadians - gyro.y * sampleDt) * next.rangeAglMeters / sampleDt : 0.0f;
	if (!isfinite(bodyVX) || !isfinite(bodyVY) || fabsf(bodyVX) > 4.0f || fabsf(bodyVY) > 4.0f) return;
	const float cy = cosf(yaw), sy = sinf(yaw);
	const float worldVX = cy * bodyVX - sy * bodyVY;
	const float worldVY = sy * bodyVX + cy * bodyVY;
	flowOrigin.vxMps += 0.25f * (worldVX - flowOrigin.vxMps);
	flowOrigin.vyMps += 0.25f * (worldVY - flowOrigin.vyMps);
	flowOrigin.xMeters += flowOrigin.vxMps * sampleDt;
	flowOrigin.yMeters += flowOrigin.vyMps * sampleDt;
	next.flowValid = true;
}
}

void updateVerticalFlightState() {
	const uint32_t nowUs = micros();
	if (lastUpdateUs && (uint32_t)(nowUs - lastUpdateUs) < 5000U) return;
	float stepSeconds = lastUpdateUs ? (float)(uint32_t)(nowUs - lastUpdateUs) * 1e-6f : 0.01f;
	lastUpdateUs = nowUs;
	stepSeconds = constrain(stepSeconds, 0.001f, 0.05f);

	const float roll = attitude.getRoll();
	const float pitch = attitude.getPitch();
	const float yaw = attitude.getYaw();
	const Vector worldSpecificForce = attitude.conjugate(acc);
	const float verticalAcceleration = worldSpecificForce.z - 9.80665f;

	BarometerEstimate barometer;
	const bool haveBarometer = getBarometerEstimate(barometer);
	DownwardRangeSample range;
	OpticalFlowSample flow;
	uint32_t rangeCount = 0, rangeFailures = 0, flowCount = 0, flowFailures = 0;
	const bool haveRange = getDownwardRangeSample(range, rangeCount, rangeFailures);
	const bool haveFlow = getOpticalFlowSample(flow, flowCount, flowFailures);
	const bool barometerUsable = haveBarometer &&
		barometerEstimateUsable(barometer, nowUs, SENSOR_MAX_AGE_US);
	const bool rawRangeUsable = haveRange && downwardRangeSampleUsable(range, nowUs, SENSOR_MAX_AGE_US, 1) &&
		range.distanceMeters <= RANGE_MAX_METERS &&
		fabsf(roll) <= FLOW_MAX_TILT_RAD && fabsf(pitch) <= FLOW_MAX_TILT_RAD;
	const float correctedRawAgl = rawRangeUsable ?
		range.distanceMeters * cosf(roll) * cosf(pitch) : 0.0f;
	const VerticalNavigationState previousEstimate = verticalEstimator.state();
	const RangeReferenceState &rangeState = rangeReference.update(correctedRawAgl, rawRangeUsable,
		armed, previousEstimate.altitudeMeters, previousEstimate.initialized && previousEstimate.healthy);
	const bool rangeUsable = rangeReferenceFusionUsable(rangeState, armed);

	VerticalNavigationInput input;
	input.nowUs = nowUs;
	input.worldAccelZMps2 = verticalAcceleration;
	input.rollRadians = 0.0f;
	input.pitchRadians = 0.0f;
	if (barometerUsable) {
		input.barometer = barometer.sample;
		input.barometer.altitudeMeters = barometer.relativeAltitudeMeters;
	}
	if (rangeUsable) {
		input.range = range;
		input.range.distanceMeters = rangeState.relativeHeightMeters;
	}
	if (haveFlow) input.flow = flow;
	verticalEstimator.update(input);
	const VerticalNavigationState estimate = verticalEstimator.state();

	VerticalFlightState next;
	next.altitudeMeters = estimate.altitudeMeters;
	next.verticalSpeedMps = estimate.verticalSpeedMps;
	next.verticalAccelerationMps2 = verticalAcceleration;
	next.timestampUs = nowUs;
	next.heightSource = (uint8_t)estimate.health;
	next.healthy = estimate.initialized && estimate.health != VerticalNavigationHealth::NoHeight;
	next.degraded = estimate.initialized && estimate.health != VerticalNavigationHealth::Fused;
	next.barometerAgeMs = haveBarometer ? (uint32_t)(nowUs - barometer.sample.timestampUs) / 1000U : UINT32_MAX;
	next.rangeAgeMs = haveRange ? (uint32_t)(nowUs - range.timestampUs) / 1000U : UINT32_MAX;
	next.flowAgeMs = haveFlow ? (uint32_t)(nowUs - flow.timestampUs) / 1000U : UINT32_MAX;
	next.flowQuality = haveFlow ? flow.quality : 0;
	next.rangeValid = rangeState.rawValid;
	next.rangeFusionValid = rangeUsable;
	next.rangeAglMeters = rangeState.rawAglMeters;
	next.rangeGroundBaselineMeters = rangeState.groundBaselineMeters;
	next.rangeRelativeHeightMeters = rangeState.relativeHeightMeters;
	next.rangeReferenceSource = (uint8_t)rangeState.source;
	if (haveFlow) updateFlowShadow(flow, range, nowUs, roll, pitch, yaw, next);
	next.flowPositionXMeters = flowOrigin.xMeters;
	next.flowPositionYMeters = flowOrigin.yMeters;
	next.flowVelocityXMps = flowOrigin.vxMps;
	next.flowVelocityYMps = flowOrigin.vyMps;
	next.controlActive = altitudeControllerActive;
	next.altitudeTargetMeters = altitudeTargetMeters;
	next.verticalSpeedTargetMps = verticalSpeedTargetMps;
	next.thrustCommand = lastVerticalThrust;
	portENTER_CRITICAL(&verticalFlightMux);
	verticalSnapshot = next;
	portEXIT_CRITICAL(&verticalFlightMux);
}

bool getVerticalFlightState(VerticalFlightState &state) {
	portENTER_CRITICAL(&verticalFlightMux);
	state = verticalSnapshot;
	portEXIT_CRITICAL(&verticalFlightMux);
	return state.timestampUs != 0;
}

bool resetNavigationOrigin(bool taskActive) {
	if (armed && taskActive) return false;
	portENTER_CRITICAL(&verticalFlightMux);
	flowOrigin.reset();
	verticalSnapshot.flowPositionXMeters = 0.0f;
	verticalSnapshot.flowPositionYMeters = 0.0f;
	verticalSnapshot.flowVelocityXMps = 0.0f;
	verticalSnapshot.flowVelocityYMps = 0.0f;
	portEXIT_CRITICAL(&verticalFlightMux);
	return true;
}

bool startOpticalFlowCalibration() {
	if (armed || motorsActive()) return false;
	flowCalibration.start(micros());
	return true;
}

void stopOpticalFlowCalibration() {
	flowCalibration.stop(micros());
}

void resetOpticalFlowCalibration() {
	if (!armed && !motorsActive()) flowCalibration.reset();
}

bool getOpticalFlowCalibrationStats(OpticalFlowCalibrationStats &stats) {
	stats = flowCalibration.stats();
	return stats.sampleCount > 0 || stats.active;
}

bool verticalFlightHealthy() {
	VerticalFlightState state;
	return getVerticalFlightState(state) && state.healthy;
}

bool enterAltitudeHold(float currentThrust) {
	VerticalFlightState state;
	if (!getVerticalFlightState(state) || !state.healthy) return false;
	verticalController.reset(currentThrust);
	altitudeTargetMeters = state.altitudeMeters;
	verticalSpeedTargetMps = state.verticalSpeedMps;
	lastVerticalThrust = currentThrust;
	altitudeControllerActive = true;
	return true;
}

void leaveAltitudeHold() {
	altitudeControllerActive = false;
	verticalController.reset(0.0f);
	verticalSpeedTargetMps = 0.0f;
}

bool applyAltitudeHoldControl(float throttleInput, float hoverThrottleInput,
	float hoverThrust, float &thrust) {
	VerticalFlightState state;
	if (!getVerticalFlightState(state) || !state.healthy) return false;
	if (!altitudeControllerActive && !enterAltitudeHold(thrust)) return false;
	const float spanUp = fmaxf(0.05f, 1.0f - hoverThrottleInput);
	const float spanDown = fmaxf(0.05f, hoverThrottleInput);
	const float stick = throttleInput - hoverThrottleInput;
	const float deadband = 0.06f;
	verticalSpeedTargetMps = 0.0f;
	if (stick > deadband) verticalSpeedTargetMps = (stick - deadband) / (spanUp - deadband) * 0.55f;
	else if (stick < -deadband) verticalSpeedTargetMps = (stick + deadband) / (spanDown - deadband) * 0.32f;
	if (fabsf(verticalSpeedTargetMps) > 0.001f)
		altitudeTargetMeters = state.altitudeMeters + verticalSpeedTargetMps / 0.75f;
	const VerticalHoldOutput output = verticalController.update(altitudeTargetMeters,
		hoverThrust, verticalEstimator.state(), micros(), true);
	thrust = output.throttle;
	lastVerticalThrust = thrust;
	return isfinite(thrust);
}

void setRouteNavigationTarget(float altitudeMeters, float headingRadians, bool valid) {
	routeTargetValid = valid && isfinite(altitudeMeters) && isfinite(headingRadians);
	if (!routeTargetValid) return;
	routeAltitudeMeters = altitudeMeters;
	routeHeadingRadians = wrapRadians(headingRadians);
}

void clearRouteNavigationTarget() {
	routeTargetValid = false;
	leaveAltitudeHold();
}

bool routeNavigationTarget(float &altitudeMeters, float &headingRadians) {
	if (!routeTargetValid) return false;
	altitudeMeters = routeAltitudeMeters;
	headingRadians = routeHeadingRadians;
	return true;
}

bool applyRouteAltitudeControl(float hoverThrust, float &thrust) {
	VerticalFlightState state;
	if (!routeTargetValid || !getVerticalFlightState(state) || !state.healthy) return false;
	if (!altitudeControllerActive && !enterAltitudeHold(thrust)) return false;
	altitudeTargetMeters = routeAltitudeMeters;
	const VerticalHoldOutput output = verticalController.update(routeAltitudeMeters,
		hoverThrust, verticalEstimator.state(), micros(), true);
	verticalSpeedTargetMps = output.targetVelocityMps;
	thrust = output.throttle;
	lastVerticalThrust = thrust;
	return isfinite(thrust);
}
