#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arduino_cli="${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}"
build_path="${1:-${TMPDIR:-/tmp}/cf-drone-esp32d-min-spiffs}"

build_flags=()
for setting in CF_DRONE_ENABLE_LOOP_STAGE_MONITOR CF_DRONE_ENABLE_FLIGHT_LOG CF_DRONE_ENABLE_WEB_INPUT_EVENT_LOG CF_DRONE_ENABLE_FAST_STOP_SERVER; do
	value="${!setting-}"
	if [[ -n "$value" ]]; then
		if [[ "$value" != 0 && "$value" != 1 ]]; then
			echo "$setting must be 0 or 1" >&2
			exit 2
		fi
		build_flags+=("-D$setting=$value")
	fi
done
if ((${#build_flags[@]})); then
	"$arduino_cli" compile \
		--fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs \
		--build-path "$build_path" \
		--build-property "compiler.cpp.extra_flags=${build_flags[*]}" \
		"$repo_root"
else
	"$arduino_cli" compile \
		--fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs \
		--build-path "$build_path" \
		"$repo_root"
fi
