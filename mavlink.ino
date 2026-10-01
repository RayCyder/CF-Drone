// MAVLink通讯
// MAVLink communication

#if WIFI_ENABLED

#include <MAVLink.h>
#include "util.h"
#include "board_config.h"
#include "control.h"
#include "flight_log.h"
#include "log_transfer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#ifndef ATTITUDE_TARGET_TYPEMASK_BODY_ROLL_RATE_IGNORE
#define ATTITUDE_TARGET_TYPEMASK_BODY_ROLL_RATE_IGNORE 1
#endif
#ifndef ATTITUDE_TARGET_TYPEMASK_BODY_PITCH_RATE_IGNORE
#define ATTITUDE_TARGET_TYPEMASK_BODY_PITCH_RATE_IGNORE 2
#endif
#ifndef ATTITUDE_TARGET_TYPEMASK_BODY_YAW_RATE_IGNORE
#define ATTITUDE_TARGET_TYPEMASK_BODY_YAW_RATE_IGNORE 4
#endif
#ifndef ATTITUDE_TARGET_TYPEMASK_THROTTLE_IGNORE
#define ATTITUDE_TARGET_TYPEMASK_THROTTLE_IGNORE 64
#endif
#ifndef ATTITUDE_TARGET_TYPEMASK_ATTITUDE_IGNORE
#define ATTITUDE_TARGET_TYPEMASK_ATTITUDE_IGNORE 128
#endif
#define ATTITUDE_TARGET_TYPEMASK_SUPPORTED ( \
	ATTITUDE_TARGET_TYPEMASK_BODY_ROLL_RATE_IGNORE | \
	ATTITUDE_TARGET_TYPEMASK_BODY_PITCH_RATE_IGNORE | \
	ATTITUDE_TARGET_TYPEMASK_BODY_YAW_RATE_IGNORE | \
	ATTITUDE_TARGET_TYPEMASK_THROTTLE_IGNORE | \
	ATTITUDE_TARGET_TYPEMASK_ATTITUDE_IGNORE)

int mavlinkSysId = 1;
Rate telemetrySlow(2);
Rate telemetryFast(BOARD_MAVLINK_TELEM_FAST_HZ);  // 遥测频率：C3=5Hz（降低WiFi占用）/ ESP32&S3=10Hz

volatile bool mavlinkConnected = false;
LogByteQueue<1024> mavlinkPrintBuffer;
static portMUX_TYPE mavlinkPrintBufferMux = portMUX_INITIALIZER_UNLOCKED;
static LogTransferCursor mavlinkLogTransfer;
static int mavlinkParameterCursor = -1;
static QueueHandle_t mavlinkRxQueue = nullptr;
static uint32_t mavlinkRxDropped = 0;
extern int wifiMode;

// UDP access and byte-level MAVLink parsing stay on the communications core.
// Parsed messages are handed to the flight loop, which remains the sole owner
// of command execution and flight-control state changes.
static void mavlinkReceiveTask(void *argument) {
	(void)argument;
	uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
	mavlink_message_t message;
	mavlink_status_t status;
	for (;;) {
		const int length = receiveWiFi(buffer, sizeof(buffer));
		if (length > 0) {
			mavlinkConnected = true;
			for (int i = 0; i < length; ++i) {
				if (mavlink_parse_char(MAVLINK_COMM_0, buffer[i], &message, &status) &&
					xQueueSend(mavlinkRxQueue, &message, 0) != pdTRUE) {
					__atomic_add_fetch(&mavlinkRxDropped, 1, __ATOMIC_RELAXED);
				}
			}
		}
		vTaskDelay(pdMS_TO_TICKS(1));
	}
}

void setupMavlinkReceiver() {
	if (wifiMode == 0) {
		print("MAVLINK_RX state=DISABLED reason=wifi_disabled\n");
		return;
	}
	mavlinkRxQueue = xQueueCreate(8, sizeof(mavlink_message_t));
	if (!mavlinkRxQueue || xTaskCreatePinnedToCore(mavlinkReceiveTask, "mavlink_rx", 4096,
		nullptr, 1, nullptr, 0) != pdPASS) {
		if (mavlinkRxQueue) { vQueueDelete(mavlinkRxQueue); mavlinkRxQueue = nullptr; }
		print("MAVLINK_RX state=DISABLED reason=task_create_failed\n");
		return;
	}
	print("MAVLINK_RX state=READY queue=8 core=0 priority=1\n");
}

uint32_t mavlinkRxDroppedCount() {
	return __atomic_load_n(&mavlinkRxDropped, __ATOMIC_RELAXED);
}

uint32_t mavlinkRxQueueDepth() {
	return mavlinkRxQueue ? (uint32_t)uxQueueMessagesWaiting(mavlinkRxQueue) : 0;
}

extern double controlTime;
extern float motors[4];
extern bool requestArm();
extern float controlRoll, controlPitch, controlThrottle, controlYaw, controlMode;

void processMavlink() {
	sendMavlink();
	receiveMavlink();
}

void sendMavlink() {
	// Bulk responses (log fragments and parameter-list entries) used to send
	// one datagram on every control-loop pass. A parameter-list request could
	// therefore flood UDP and stall the loop in the Wi-Fi stack for tens of ms.
	// Share a bounded 50 Hz budget across console output and bulk transfers.
	static Rate bulkTransferRate(50);
	bool deferTelemetry = false;
	if (armed || motorsActive()) {
		sendMavlinkPrint();
	} else if (bulkTransferRate) {
		deferTelemetry = true;
		if (mavlinkLogTransfer.active) serviceMavlinkLogTransfer();
		else if (mavlinkParameterCursor >= 0 && mavlinkParameterCursor < parametersCount())
			serviceMavlinkParameterList();
		else sendMavlinkPrint();
	}

	// Spread telemetry message packing across loop iterations. Keeping a burst
	// of five packets in one pass made the MAVLink stage itself approach 1 ms.
	static uint8_t pendingSlowTelemetry = 0;
	static uint8_t pendingFastTelemetry = 0;
	if (telemetrySlow) {
		pendingSlowTelemetry |= 0x01; // heartbeat
		if (mavlinkConnected) pendingSlowTelemetry |= 0x02; // extended state
	}
	if (!mavlinkConnected) {
		pendingSlowTelemetry &= (uint8_t)~0x02;
		pendingFastTelemetry = 0;
	} else if (telemetryFast) {
		pendingFastTelemetry |= 0x0f;
	}

	if (deferTelemetry) return;
	mavlink_message_t msg;
	const uint32_t time = (uint32_t)(uint64_t)(t * 1000.0);
	if (pendingSlowTelemetry & 0x01) {
		pendingSlowTelemetry &= (uint8_t)~0x01;
		mavlink_msg_heartbeat_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_GENERIC,
			(armed ? MAV_MODE_FLAG_SAFETY_ARMED : 0) |
			((mode == STAB) ? MAV_MODE_FLAG_STABILIZE_ENABLED : 0) |
			((mode == AUTO) ? MAV_MODE_FLAG_AUTO_ENABLED : MAV_MODE_FLAG_MANUAL_INPUT_ENABLED),
			mode, MAV_STATE_STANDBY);
		sendMessage(&msg);
	} else if (pendingSlowTelemetry & 0x02) {
		pendingSlowTelemetry &= (uint8_t)~0x02;
		mavlink_msg_extended_sys_state_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg,
			MAV_VTOL_STATE_UNDEFINED, landed ? MAV_LANDED_STATE_ON_GROUND : MAV_LANDED_STATE_IN_AIR);
		sendMessage(&msg);
	} else if (pendingFastTelemetry & 0x01) {
		pendingFastTelemetry &= (uint8_t)~0x01;
		const float offset[] = {0, 0, 0, 0};
		mavlink_msg_attitude_quaternion_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg,
			time, attitude.w, attitude.x, -attitude.y, -attitude.z, rates.x, -rates.y, -rates.z, offset);
		sendMessage(&msg);
	} else if (pendingFastTelemetry & 0x02) {
		pendingFastTelemetry &= (uint8_t)~0x02;
		if (channels[0] != 0) {
			mavlink_msg_rc_channels_raw_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg, controlTime * 1000, 0,
				channels[0], channels[1], channels[2], channels[3], channels[4], channels[5], channels[6], channels[7], UINT8_MAX);
			sendMessage(&msg);
		}
	} else if (pendingFastTelemetry & 0x04) {
		pendingFastTelemetry &= (uint8_t)~0x04;
		float controls[8] = {};
		memcpy(controls, motors, sizeof(motors));
		mavlink_msg_actuator_control_target_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg, time, 0, controls);
		sendMessage(&msg);
	} else if (pendingFastTelemetry & 0x08) {
		pendingFastTelemetry &= (uint8_t)~0x08;
		mavlink_msg_scaled_imu_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg, time,
			acc.x / ONE_G * 1000, -acc.y / ONE_G * 1000, -acc.z / ONE_G * 1000,
			gyro.x * 1000, -gyro.y * 1000, -gyro.z * 1000,
			0, 0, 0, 0);
		sendMessage(&msg);
	}
}

void sendMessage(const void *msg) {
	uint8_t buf[MAVLINK_MAX_PACKET_LEN];
	int len = mavlink_msg_to_send_buffer(buf, (mavlink_message_t *)msg);
	sendWiFi(buf, len);
}

void receiveMavlink() {
	// Bound command work per flight-loop pass; parsing and socket reads happen
	// in mavlinkReceiveTask, while stateful commands execute only here.
	constexpr uint8_t MAX_MESSAGES_PER_PASS = 2;
	if (!mavlinkRxQueue) return;
	mavlink_message_t message;
	for (uint8_t i = 0; i < MAX_MESSAGES_PER_PASS &&
		 xQueueReceive(mavlinkRxQueue, &message, 0) == pdTRUE; ++i) {
		handleMavlink(&message);
	}
}

void handleMavlink(const void *_msg) {
	const mavlink_message_t& msg = *(mavlink_message_t *)_msg;

	if (msg.msgid == MAVLINK_MSG_ID_MANUAL_CONTROL) {
		mavlink_manual_control_t m;
		mavlink_msg_manual_control_decode(&msg, &m);
		if (m.target && m.target != mavlinkSysId) return; // 0 is broadcast
		if (m.x < -1000 || m.x > 1000 || m.y < -1000 || m.y > 1000 ||
			m.r < -1000 || m.r > 1000 || m.z < 0 || m.z > 1000)
			return;
		if (!canAcceptMavlinkManualControl()) return;

		controlThrottle = m.z / 1000.0f;
		controlPitch = m.x / 1000.0f;
		controlRoll = m.y / 1000.0f;
		controlYaw = m.r / 1000.0f;
		controlMode = NAN;
		controlTime = t;
		markManualControlInput(CONTROL_SOURCE_MAVLINK_MANUAL);
	}

	if (msg.msgid == MAVLINK_MSG_ID_PARAM_REQUEST_LIST) {
		mavlink_param_request_list_t m;
		mavlink_msg_param_request_list_decode(&msg, &m);
		if (m.target_system && m.target_system != mavlinkSysId) return;

        if (armed || motorsActive()) return;
        mavlinkParameterCursor = 0;
	}

	if (msg.msgid == MAVLINK_MSG_ID_PARAM_REQUEST_READ) {
		mavlink_param_request_read_t m;
		mavlink_msg_param_request_read_decode(&msg, &m);
		if (m.target_system && m.target_system != mavlinkSysId) return;

		char name[MAVLINK_MSG_PARAM_REQUEST_READ_FIELD_PARAM_ID_LEN + 1];
        if (armed || motorsActive()) return;
        memcpy(name, m.param_id, sizeof(name)-1); name[sizeof(name)-1] = 0;
        if (m.param_index < -1 || m.param_index >= parametersCount()) return;
        float value = m.param_index >= 0 ? getParameter(m.param_index) : getParameter(name);
        if (m.param_index >= 0) snprintf(name, sizeof(name), "%s", getParameterName(m.param_index));
		mavlink_message_t msg;
		mavlink_msg_param_value_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg,
			name, value, MAV_PARAM_TYPE_REAL32, parametersCount(), m.param_index);
		sendMessage(&msg);
	}

	if (msg.msgid == MAVLINK_MSG_ID_PARAM_SET) {
		mavlink_param_set_t m;
		mavlink_msg_param_set_decode(&msg, &m);
		if (m.target_system && m.target_system != mavlinkSysId) return;

		char name[MAVLINK_MSG_PARAM_SET_FIELD_PARAM_ID_LEN + 1];
		if (armed || motorsActive()) return;
        memcpy(name, m.param_id, sizeof(name)-1); name[sizeof(name)-1] = 0;
		bool success = setParameter(name, m.param_value);
		if (!success) return;
		// send ack
		mavlink_message_t msg;
		mavlink_msg_param_value_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg,
			m.param_id, getParameter(name), MAV_PARAM_TYPE_REAL32, parametersCount(), 0); // index is unknown
		sendMessage(&msg);
	}

	if (msg.msgid == MAVLINK_MSG_ID_MISSION_REQUEST_LIST) { // handle to make qgc happy
		mavlink_mission_request_list_t m;
		mavlink_msg_mission_request_list_decode(&msg, &m);
		if (m.target_system && m.target_system != mavlinkSysId) return;

		mavlink_message_t msg;
		mavlink_msg_mission_count_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg, 0, 0, 0, MAV_MISSION_TYPE_MISSION, 0);
		sendMessage(&msg);
	}

	if (msg.msgid == MAVLINK_MSG_ID_SERIAL_CONTROL) {
		mavlink_serial_control_t m;
		mavlink_msg_serial_control_decode(&msg, &m);
		if (m.target_system && m.target_system != mavlinkSysId) return;

        if (m.target_component && m.target_component != MAV_COMP_ID_AUTOPILOT1) return;
        if (m.device != SERIAL_CONTROL_DEV_SHELL || m.count > sizeof(m.data)) return;
        char data[MAVLINK_MSG_SERIAL_CONTROL_FIELD_DATA_LEN + 1];
        memcpy(data, m.data, m.count); data[m.count] = 0;
        setMavlinkConsoleCommandOutput(true);
        doCommand(data, true);
        setMavlinkConsoleCommandOutput(false);
	}

	if (msg.msgid == MAVLINK_MSG_ID_SET_ATTITUDE_TARGET) {
		mavlink_set_attitude_target_t m;
		mavlink_msg_set_attitude_target_decode(&msg, &m);
		if (m.target_system && m.target_system != mavlinkSysId) return;
		if (m.target_component && m.target_component != MAV_COMP_ID_AUTOPILOT1) return;

		if (m.type_mask & ~ATTITUDE_TARGET_TYPEMASK_SUPPORTED) return;
		const bool ignoreRollRate = m.type_mask & ATTITUDE_TARGET_TYPEMASK_BODY_ROLL_RATE_IGNORE;
		const bool ignorePitchRate = m.type_mask & ATTITUDE_TARGET_TYPEMASK_BODY_PITCH_RATE_IGNORE;
		const bool ignoreYawRate = m.type_mask & ATTITUDE_TARGET_TYPEMASK_BODY_YAW_RATE_IGNORE;
		const bool anyRateIgnored = ignoreRollRate || ignorePitchRate || ignoreYawRate;
		const bool allRatesIgnored = ignoreRollRate && ignorePitchRate && ignoreYawRate;
		if (anyRateIgnored && !allRatesIgnored) return;
		if (m.type_mask & ATTITUDE_TARGET_TYPEMASK_THROTTLE_IGNORE) return;

		AutoAttitudeCommand target;
		target.useAttitude = !(m.type_mask & ATTITUDE_TARGET_TYPEMASK_ATTITUDE_IGNORE);
		target.useRates = !allRatesIgnored;
		if (!target.useAttitude && !target.useRates) return;
		target.attitude = Quaternion(m.q[0], m.q[1], -m.q[2], -m.q[3]); // FRD -> FLU
		target.rates = Vector(m.body_roll_rate, -m.body_pitch_rate, -m.body_yaw_rate);
		target.thrust = m.thrust;
		submitAutoAttitudeTarget(target);
	}

	if (msg.msgid == MAVLINK_MSG_ID_SET_ACTUATOR_CONTROL_TARGET) {
		mavlink_set_actuator_control_target_t m;
		mavlink_msg_set_actuator_control_target_decode(&msg, &m);
		if (m.target_system && m.target_system != mavlinkSysId) return;
		if (m.target_component && m.target_component != MAV_COMP_ID_AUTOPILOT1) return;
		if (m.group_mlx != 0) return;

		AutoActuatorCommand target;
		for (int i = 0; i < 4; ++i) target.motors[i] = m.controls[i];
		submitAutoActuatorTarget(target);
	}

    if (msg.msgid == MAVLINK_MSG_ID_LOG_REQUEST_END) {
        mavlink_log_request_end_t m; mavlink_msg_log_request_end_decode(&msg, &m);
        if ((!m.target_system || m.target_system == mavlinkSysId) &&
            (!m.target_component || m.target_component == MAV_COMP_ID_AUTOPILOT1))
            mavlinkLogTransfer.active = false;
    }
    if (msg.msgid == MAVLINK_MSG_ID_LOG_REQUEST_LIST) {
        mavlink_log_request_list_t m; mavlink_msg_log_request_list_decode(&msg, &m);
        if ((m.target_system && m.target_system != mavlinkSysId) ||
            (m.target_component && m.target_component != MAV_COMP_ID_AUTOPILOT1) || m.start != 0 ||
            armed || motorsActive() || !freezeFlightLog()) return;
        const FlightLogStatus status = getFlightLogStatus();
        mavlink_message_t response;
        mavlink_msg_log_entry_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &response, 0, 1, 0, 0,
            status.rowCount * FLIGHT_LOG_LEGACY_ROW_BYTES);
        sendMessage(&response);
    }
    if (msg.msgid == MAVLINK_MSG_ID_LOG_REQUEST_DATA) {
        mavlink_log_request_data_t m; mavlink_msg_log_request_data_decode(&msg, &m);
        if ((m.target_system && m.target_system != mavlinkSysId) ||
            (m.target_component && m.target_component != MAV_COMP_ID_AUTOPILOT1) || m.id != 0 ||
            armed || motorsActive() || !freezeFlightLog()) return;
        const FlightLogStatus status = getFlightLogStatus();
        mavlinkLogTransfer.start(status.generation, status.rowCount * FLIGHT_LOG_LEGACY_ROW_BYTES,
            m.ofs, m.count);
    }

	// Handle commands
	if (msg.msgid == MAVLINK_MSG_ID_COMMAND_LONG) {
		mavlink_command_long_t m;
		mavlink_msg_command_long_decode(&msg, &m);
		if (m.target_system && m.target_system != mavlinkSysId) return;
		mavlink_message_t response;
		bool accepted = false;
		uint8_t result = MAV_RESULT_UNSUPPORTED;

		if (m.command == MAV_CMD_REQUEST_MESSAGE && m.param1 == MAVLINK_MSG_ID_AUTOPILOT_VERSION) {
			accepted = true;
			mavlink_msg_autopilot_version_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &response,
				MAV_PROTOCOL_CAPABILITY_PARAM_FLOAT | MAV_PROTOCOL_CAPABILITY_MAVLINK2, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0);
			sendMessage(&response);
		}

		if (m.command == MAV_CMD_COMPONENT_ARM_DISARM) {
			if (m.param1 == 0) {
				disarm(DISARM_REASON_MAVLINK);
				accepted = true;
			} else if (m.param1 == 1) {
				accepted = requestArm();
			}
			result = accepted ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED;
		}

		if (m.command == MAV_CMD_DO_SET_MODE) {
			if (isfinite(m.param2) && floorf(m.param2) == m.param2 &&
				m.param2 >= RAW && m.param2 <= AUTO) {
				accepted = setFlightMode((int)m.param2);
			}
			result = accepted ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED;
		}

		// send command ack
		mavlink_message_t ack;
		mavlink_msg_command_ack_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &ack, m.command, accepted ? MAV_RESULT_ACCEPTED : result, UINT8_MAX, 0, msg.sysid, msg.compid);
		sendMessage(&ack);
	}
}

// Each maintenance pass sends at most one fragment per bounded transfer.
void serviceMavlinkLogTransfer() {
    if (!mavlinkLogTransfer.active) return;
    const FlightLogStatus status = getFlightLogStatus();
    if (armed || motorsActive() || status.state != FROZEN ||
        status.generation != mavlinkLogTransfer.generation) { mavlinkLogTransfer.active = false; return; }
    uint8_t data[90] = {};
    const uint8_t wanted = mavlinkLogTransfer.nextCount();
    const size_t count = readFrozenLogBytes(mavlinkLogTransfer.generation,
        mavlinkLogTransfer.offset, data, wanted);
    if (count != wanted) { mavlinkLogTransfer.active = false; return; }
    mavlink_message_t response;
    mavlink_msg_log_data_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &response, 0,
        mavlinkLogTransfer.offset, (uint8_t)count, data);
    sendMessage(&response);
    mavlinkLogTransfer.advance((uint8_t)count);
}

void serviceMavlinkParameterList() {
    if (armed || motorsActive()) { mavlinkParameterCursor = -1; return; }
    if (mavlinkParameterCursor < 0 || mavlinkParameterCursor >= parametersCount()) return;
    const int i = mavlinkParameterCursor++;
    mavlink_message_t response;
    mavlink_msg_param_value_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &response,
        getParameterName(i), getParameter(i), MAV_PARAM_TYPE_REAL32, parametersCount(), i);
    sendMessage(&response);
}

void mavlinkPrint(const char* str) {
    if (!armed && !motorsActive() && mavlinkConnected) {
        portENTER_CRITICAL(&mavlinkPrintBufferMux);
        mavlinkPrintBuffer.push(str);
        portEXIT_CRITICAL(&mavlinkPrintBufferMux);
    }
}

void sendMavlinkPrint() {
    if (armed || motorsActive()) {
        portENTER_CRITICAL(&mavlinkPrintBufferMux);
        mavlinkPrintBuffer.clear();
        portEXIT_CRITICAL(&mavlinkPrintBufferMux);
        return;
    }
    uint8_t data[MAVLINK_MSG_SERIAL_CONTROL_FIELD_DATA_LEN] = {};
    uint32_t dropped = 0;
    bool more = false;
    portENTER_CRITICAL(&mavlinkPrintBufferMux);
    size_t count = mavlinkPrintBuffer.pop(data, sizeof(data));
    if (!count && mavlinkPrintBuffer.dropped) {
        dropped = mavlinkPrintBuffer.dropped;
        mavlinkPrintBuffer.dropped = 0;
    }
    more = mavlinkPrintBuffer.size() != 0;
    portEXIT_CRITICAL(&mavlinkPrintBufferMux);
    if (!count && dropped) {
        count = snprintf((char *)data, sizeof(data), "[console overflow: %lu bytes dropped]\n",
            (unsigned long)dropped);
        if (count >= sizeof(data)) count = sizeof(data)-1;
    }
    if (!count) return;
    mavlink_message_t msg;
    mavlink_msg_serial_control_pack(mavlinkSysId, MAV_COMP_ID_AUTOPILOT1, &msg,
        SERIAL_CONTROL_DEV_SHELL, more ? SERIAL_CONTROL_FLAG_MULTI : 0,
        0, 0, (uint8_t)count, data, 0, 0);
    sendMessage(&msg);
}

#endif
