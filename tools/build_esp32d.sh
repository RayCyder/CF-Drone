#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arduino_cli="${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}"
build_path="${1:-${TMPDIR:-/tmp}/cf-drone-esp32d-min-spiffs}"

"$arduino_cli" compile \
	--fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs \
	--build-path "$build_path" \
	"$repo_root"
