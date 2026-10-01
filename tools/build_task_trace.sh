#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cli=${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}
fqbn=${1:-esp32:esp32:esp32}
build_dir=${2:-/private/tmp/cf-drone-task-trace-build}
trace_mode=${3:-full}

case "$trace_mode" in
    full)
        trace_defines=
        trace_wraps=vTaskSwitchContext,--wrap=esp_ipc_call_nonblocking,--wrap=spi_flash_op_block_func
        ;;
    ipc-only)
        trace_defines=-DCF_DRONE_DISABLE_SCHEDULER_TRACE_HOOK
        trace_wraps=esp_ipc_call_nonblocking,--wrap=spi_flash_op_block_func
        ;;
    scheduler-only)
        trace_defines=-DCF_DRONE_DISABLE_IPC_TRACE_HOOK
        trace_wraps=vTaskSwitchContext
        ;;
    armed-loop)
        trace_defines=-DCF_DRONE_CAPTURE_ARMED_LOOP_TRACE
        trace_wraps=
        ;;
    *)
        echo "Usage: $0 [fqbn] [build_dir] [full|ipc-only|scheduler-only|armed-loop]" >&2
        exit 2
        ;;
esac

if [ ! -x "$cli" ]; then
    echo "Arduino CLI not found at $cli; set ARDUINO_CLI to its executable path" >&2
    exit 1
fi

if [ -n "$trace_wraps" ]; then
    "$cli" compile \
        --fqbn "$fqbn" \
        --build-path "$build_dir" \
        --build-property "compiler.cpp.extra_flags=-DCF_DRONE_ENABLE_TASK_SWITCH_TRACE $trace_defines" \
        --build-property "compiler.c.elf.extra_flags=-Wl,--wrap=$trace_wraps" \
        "$repo_dir"
else
    "$cli" compile \
        --fqbn "$fqbn" \
        --build-path "$build_dir" \
        --build-property "compiler.cpp.extra_flags=$trace_defines" \
        "$repo_dir"
fi
