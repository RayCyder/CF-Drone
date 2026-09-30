#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cli=${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}
fqbn=${1:-esp32:esp32:esp32}
build_dir=${2:-/private/tmp/cf-drone-task-trace-build}

if [ ! -x "$cli" ]; then
    echo "Arduino CLI not found at $cli; set ARDUINO_CLI to its executable path" >&2
    exit 1
fi

"$cli" compile \
    --fqbn "$fqbn" \
    --build-path "$build_dir" \
    --build-property compiler.cpp.extra_flags=-DCF_DRONE_ENABLE_TASK_SWITCH_TRACE \
    --build-property compiler.c.elf.extra_flags=-Wl,--wrap=vTaskSwitchContext,--wrap=esp_ipc_call_nonblocking,--wrap=spi_flash_op_block_func \
    "$repo_dir"
