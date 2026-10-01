#pragma once

#include <cstdint>
#include <cstring>

#define MAVLINK_MAX_PACKET_LEN 280
#define MAVLINK_COMM_0 0
#define MAVLINK_MSG_ID_MANUAL_CONTROL 69
#define MAVLINK_MSG_ID_PARAM_REQUEST_LIST 21
#define MAVLINK_MSG_ID_PARAM_REQUEST_READ 20
#define MAVLINK_MSG_ID_PARAM_SET 23
#define MAVLINK_MSG_ID_MISSION_REQUEST_LIST 43
#define MAVLINK_MSG_ID_SERIAL_CONTROL 126
#define MAVLINK_MSG_ID_SET_ATTITUDE_TARGET 82
#define MAVLINK_MSG_ID_SET_ACTUATOR_CONTROL_TARGET 139
#define MAVLINK_MSG_ID_LOG_REQUEST_END 122
#define MAVLINK_MSG_ID_LOG_REQUEST_LIST 117
#define MAVLINK_MSG_ID_LOG_REQUEST_DATA 119
#define MAVLINK_MSG_ID_COMMAND_LONG 76
#define MAVLINK_MSG_ID_AUTOPILOT_VERSION 148
#define MAV_COMP_ID_AUTOPILOT1 1
#define MAV_TYPE_QUADROTOR 2
#define MAV_AUTOPILOT_GENERIC 0
#define MAV_MODE_FLAG_SAFETY_ARMED 128
#define MAV_MODE_FLAG_STABILIZE_ENABLED 16
#define MAV_MODE_FLAG_AUTO_ENABLED 4
#define MAV_MODE_FLAG_MANUAL_INPUT_ENABLED 64
#define MAV_STATE_STANDBY 3
#define MAV_VTOL_STATE_UNDEFINED 0
#define MAV_LANDED_STATE_ON_GROUND 1
#define MAV_LANDED_STATE_IN_AIR 2
#define MAV_PARAM_TYPE_REAL32 9
#define MAV_MISSION_TYPE_MISSION 0
#define SERIAL_CONTROL_DEV_SHELL 10
#define SERIAL_CONTROL_FLAG_MULTI 1
#define MAV_PROTOCOL_CAPABILITY_PARAM_FLOAT 1
#define MAV_PROTOCOL_CAPABILITY_MAVLINK2 8192
#define MAV_CMD_REQUEST_MESSAGE 512
#define MAV_CMD_COMPONENT_ARM_DISARM 400
#define MAV_CMD_DO_SET_MODE 176
#define MAV_RESULT_ACCEPTED 0
#define MAV_RESULT_DENIED 2
#define MAV_RESULT_UNSUPPORTED 3
#define MAVLINK_MSG_PARAM_REQUEST_READ_FIELD_PARAM_ID_LEN 16
#define MAVLINK_MSG_PARAM_SET_FIELD_PARAM_ID_LEN 16
#define MAVLINK_MSG_SERIAL_CONTROL_FIELD_DATA_LEN 70

struct mavlink_manual_control_t { int16_t x = 0, y = 0, z = 0, r = 0; uint16_t buttons = 0; uint8_t target = 0; };
struct mavlink_param_request_list_t { uint8_t target_system = 0; };
struct mavlink_param_request_read_t { uint8_t target_system = 0; int16_t param_index = -1; char param_id[MAVLINK_MSG_PARAM_REQUEST_READ_FIELD_PARAM_ID_LEN] = {}; };
struct mavlink_param_set_t { uint8_t target_system = 0; float param_value = 0; char param_id[MAVLINK_MSG_PARAM_SET_FIELD_PARAM_ID_LEN] = {}; };
struct mavlink_mission_request_list_t { uint8_t target_system = 0; };
struct mavlink_serial_control_t { uint8_t target_system = 0, target_component = 0, device = 0, count = 0; uint8_t data[MAVLINK_MSG_SERIAL_CONTROL_FIELD_DATA_LEN] = {}; };
struct mavlink_set_attitude_target_t { uint8_t target_system = 0, target_component = 0; uint8_t type_mask = 0; float q[4] = {}; float body_roll_rate = 0, body_pitch_rate = 0, body_yaw_rate = 0, thrust = 0; };
struct mavlink_set_actuator_control_target_t { uint8_t target_system = 0, target_component = 0, group_mlx = 0; float controls[8] = {}; };
struct mavlink_log_request_end_t { uint8_t target_system = 0, target_component = 0; };
struct mavlink_log_request_list_t { uint8_t target_system = 0, target_component = 0; uint16_t start = 0; };
struct mavlink_log_request_data_t { uint8_t target_system = 0, target_component = 0; uint16_t id = 0; uint32_t ofs = 0, count = 0; };
struct mavlink_command_long_t { uint8_t target_system = 0; uint16_t command = 0; float param1 = 0, param2 = 0; };
struct mavlink_status_t {};

struct mavlink_message_t {
    uint32_t msgid = 0;
    uint8_t sysid = 1;
    uint8_t compid = 1;
    mavlink_manual_control_t manual;
    mavlink_param_request_list_t paramRequestList;
    mavlink_param_request_read_t paramRequestRead;
    mavlink_param_set_t paramSet;
    mavlink_mission_request_list_t missionRequestList;
    mavlink_serial_control_t serialControl;
    mavlink_set_attitude_target_t attitudeTarget;
    mavlink_set_actuator_control_target_t actuatorTarget;
    mavlink_log_request_end_t logRequestEnd;
    mavlink_log_request_list_t logRequestList;
    mavlink_log_request_data_t logRequestData;
    mavlink_command_long_t commandLong;
};

extern mavlink_message_t testLastPackedMessage;

inline bool mavlink_parse_char(int, uint8_t, mavlink_message_t*, mavlink_status_t*) { return false; }
inline int mavlink_msg_to_send_buffer(uint8_t*, mavlink_message_t* msg) { testLastPackedMessage = *msg; return 0; }
inline void mavlink_msg_manual_control_decode(const mavlink_message_t* msg, mavlink_manual_control_t* out) { *out = msg->manual; }
inline void mavlink_msg_param_request_list_decode(const mavlink_message_t* msg, mavlink_param_request_list_t* out) { *out = msg->paramRequestList; }
inline void mavlink_msg_param_request_read_decode(const mavlink_message_t* msg, mavlink_param_request_read_t* out) { *out = msg->paramRequestRead; }
inline void mavlink_msg_param_set_decode(const mavlink_message_t* msg, mavlink_param_set_t* out) { *out = msg->paramSet; }
inline void mavlink_msg_mission_request_list_decode(const mavlink_message_t* msg, mavlink_mission_request_list_t* out) { *out = msg->missionRequestList; }
inline void mavlink_msg_serial_control_decode(const mavlink_message_t* msg, mavlink_serial_control_t* out) { *out = msg->serialControl; }
inline void mavlink_msg_set_attitude_target_decode(const mavlink_message_t* msg, mavlink_set_attitude_target_t* out) { *out = msg->attitudeTarget; }
inline void mavlink_msg_set_actuator_control_target_decode(const mavlink_message_t* msg, mavlink_set_actuator_control_target_t* out) { *out = msg->actuatorTarget; }
inline void mavlink_msg_log_request_end_decode(const mavlink_message_t* msg, mavlink_log_request_end_t* out) { *out = msg->logRequestEnd; }
inline void mavlink_msg_log_request_list_decode(const mavlink_message_t* msg, mavlink_log_request_list_t* out) { *out = msg->logRequestList; }
inline void mavlink_msg_log_request_data_decode(const mavlink_message_t* msg, mavlink_log_request_data_t* out) { *out = msg->logRequestData; }
inline void mavlink_msg_command_long_decode(const mavlink_message_t* msg, mavlink_command_long_t* out) { *out = msg->commandLong; }

inline void mavlink_msg_heartbeat_pack(...) {}
inline void mavlink_msg_extended_sys_state_pack(...) {}
inline void mavlink_msg_attitude_quaternion_pack(...) {}
inline void mavlink_msg_rc_channels_raw_pack(...) {}
inline void mavlink_msg_actuator_control_target_pack(...) {}
inline void mavlink_msg_scaled_imu_pack(...) {}
inline void mavlink_msg_param_value_pack(...) {}
inline void mavlink_msg_mission_count_pack(...) {}
inline void mavlink_msg_log_entry_pack(...) {}
inline void mavlink_msg_log_data_pack(...) {}
inline void mavlink_msg_autopilot_version_pack(...) {}
inline void mavlink_msg_command_ack_pack(...) {}
inline void mavlink_msg_serial_control_pack(int, int, mavlink_message_t* msg, uint8_t, uint8_t flags, uint16_t, uint32_t, uint8_t count, const uint8_t* data, uint8_t, uint8_t) {
    msg->msgid = MAVLINK_MSG_ID_SERIAL_CONTROL;
    msg->serialControl.count = count;
    msg->serialControl.target_component = flags;
    if (data && count <= MAVLINK_MSG_SERIAL_CONTROL_FIELD_DATA_LEN) std::memcpy(msg->serialControl.data, data, count);
}
