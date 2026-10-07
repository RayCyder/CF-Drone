#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arduino_cli="${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}"
build_path="${1:-${TMPDIR:-/tmp}/cf-drone-esp32d-min-spiffs}"
profile_input="${2:-${ESP32D_SENSOR_PROFILE:-full}}"

case "$profile_input" in
	1|imu|imu-only)
		profile_id=1
		profile_name="imu"
		;;
	2|baro-mag|baro_mag|navigation)
		profile_id=2
		profile_name="baro-mag"
		;;
	3|full|full-sensors|full_sensors)
		profile_id=3
		profile_name="full"
		;;
	*)
		echo "Unknown ESP32-D sensor profile: $profile_input" >&2
		echo "Use one of: imu, baro-mag, full (or 1, 2, 3)." >&2
		exit 2
		;;
esac

build_flags=("-DCF_DRONE_ESP32D_SENSOR_PROFILE=$profile_id")
for setting in CF_DRONE_ENABLE_LOOP_STAGE_MONITOR CF_DRONE_ENABLE_FLIGHT_LOG CF_DRONE_ENABLE_WEB_INPUT_EVENT_LOG CF_DRONE_ENABLE_FAST_STOP_SERVER CF_DRONE_ENABLE_MAVLINK CF_DRONE_ENABLE_BOOT_MOTOR_SELF_CHECK; do
	value="${!setting-}"
	if [[ -n "$value" ]]; then
		if [[ "$value" != 0 && "$value" != 1 ]]; then
			echo "$setting must be 0 or 1" >&2
			exit 2
		fi
		build_flags+=("-D$setting=$value")
	fi
done
echo "ESP32-D sensor profile: $profile_name ($profile_id)"
"$arduino_cli" compile \
	--fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs \
	--build-path "$build_path" \
	--build-property "compiler.cpp.extra_flags=${build_flags[*]}" \
	"$repo_root"

cat >"$build_path/cf-drone-build-profile.txt" <<EOF
board=esp32-d
sensor_profile=$profile_name
sensor_profile_id=$profile_id
fqbn=esp32:esp32:esp32:PartitionScheme=min_spiffs
EOF
