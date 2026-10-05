const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'web_rc_html_controller.h'), 'utf8');

assert.equal((source.match(/id="console-open-button"/g) || []).length, 1,
  'the debug entry has one stable DOM id');
assert.match(source,
  /<nav class="header-tools"[\s\S]*id="console-open-button"[\s\S]*>调试<\/button>[\s\S]*<\/nav>/,
  'debug is in the former motor-test header position');
assert.doesNotMatch(source, /id="vibration-calibration-button"/,
  'motor disturbance test is no longer a top-level header button');
assert.match(source,
  /openVibrationCalibrationFromConsole\(\)[\s\S]*openVibrationCalibrationPage\(\)/,
  'the motor disturbance test remains available from the debug interface');

for (const command of ['diag brief', 'diag', 'imu', 'ps', 'rc', 'mot', 'wifi',
  'time', 'sys', 'log status', 'p', 'help', 'p CTL_TRIM_ROLL', 'p CTL_TRIM_PITCH']) {
  assert.ok(source.includes(`runConsoleCommand('${command}')`),
    `common command button exists for ${command}`);
}
assert.match(source,
  /startAccelCalibrationFromConsole\(\)[\s\S]*confirm\([\s\S]*自动保存[\s\S]*runConsoleCommand\('ca'\)/,
  'accelerometer calibration shortcut explains automatic persistence before starting ca');
assert.match(source,
  /磁力计为可选传感器；未安装磁力计仍可进行六面加速度计校准和机身水平校准/,
  'debug tools explain that inertial calibrations do not require a compass');
assert.match(source,
  /openLevelCalibrationFromConsole\(\)[\s\S]*openLevelCalibrationPage\(\)/,
  'debug tools expose the guided body-level calibration workflow');
assert.doesNotMatch(source,
  /class="footer"[^\n]*openLevelCalibrationPage/,
  'body-level calibration is not duplicated in the page footer');
assert.match(source,
  /机身静置水平但姿态不为 0°[\s\S]*CTL_TRIM_ROLL \/ CTL_TRIM_PITCH/,
  'debug tools distinguish installation-angle calibration from in-flight trim');
assert.match(source, /const CONSOLE_REQUEST_TIMEOUT_MS = 8000;/,
  'console polling tolerates slow mobile Wi-Fi responses');
assert.match(source,
  /if\(consoleReadFailureCount>0\)setConsoleStatus\('控制台连接已恢复。','ok'\)/,
  'a successful poll clears a stale timeout status');
assert.match(source,
  /@media \(max-height:480px\) and \(orientation:landscape\)[\s\S]*\.console-panel\{display:grid;[\s\S]*\.console-tools\{grid-column:1;[\s\S]*\.console-output\{grid-column:2;[\s\S]*\.console-command-row\{grid-column:2;/,
  'small landscape layout keeps tools beside the log and command row');

assert.match(source,
  /@media \(max-height:480px\) and \(orientation:landscape\)[\s\S]*\.container\{height:100dvh;min-height:0;overflow:hidden\}[\s\S]*\.header h1\{display:none\}[\s\S]*\.footer\{display:none\}/,
  'small landscape layout hides the title and footer and prevents main-page scrolling');
assert.match(source,
  /松手回悬停油门[\s\S]*id="hover-throttle-label"/,
  'left stick explains its spring return target');
assert.match(source,
  /function returnLeftStickToHover\(\)[\s\S]*duration=300[\s\S]*hoverThrottleRaw-startRawY[\s\S]*requestAnimationFrame\(step\)/,
  'left throttle returns smoothly to the configured hover position');
assert.match(source,
  /hover_throttle_pct[\s\S]*hoverThrottleRaw=bounded\*2-100/,
  'the page derives its release position from firmware status');
assert.match(source,
  /let stickInputActivated = false;[\s\S]*if \(stickInputActivated && \(hasSignificantChange/,
  'a newly opened page does not send its initial zero throttle before explicit input');
assert.match(source,
  /function handlePointerStart\(e, side\)[\s\S]*stickInputActivated=true/,
  'touching either stick explicitly activates joystick transmission');
assert.ok(source.includes('hover_throttle_reachable') && source.includes('悬停推力不可达'),
  'the page warns when throttle scaling makes hover thrust unreachable');
assert.match(source,
  /async function handleButton\(idx\)[\s\S]*idx===0&&!currentArmed[\s\S]*leftStick\.rawY=-100[\s\S]*if\(!await sendJoystickData\(\)\)[\s\S]*未发送解锁[\s\S]*sendButtonData\(idx, 1\)/,
  'arming explicitly sends and confirms zero throttle before the arm command');
assert.match(source,
  /function sendJoystickData\(\)[\s\S]*lastSentValues = values;[\s\S]*packetStats\.sent\+\+;[\s\S]*return joystickRequestPromise;/,
  'joystick send accounting remains reachable while returning the serialized request');
assert.match(source,
  /webRCStopToken = localStorage\.getItem\('cfDroneStopToken'\)[\s\S]*sessionStorage\.getItem\('cfDroneStopToken'\)/,
  'the flight continuity token survives an accidental page refresh or browser restart');
assert.match(source,
  /web_rc_flight_takeover_forbidden[\s\S]*飞行中禁止其他页面抢占/,
  'the controller explains why a different page cannot take over in flight');
assert.match(source,
  /new URL\(raw, location\.href\)[\s\S]*url\.origin === location\.origin && flightRequestPaths\.has\(url\.pathname\)/,
  'the armed-request filter normalizes relative and absolute request URLs');
assert.match(source,
  /nativeFetch\(`\$\{location\.protocol\}\/\/\$\{location\.hostname\}:82\/\$\{action\}/,
  'emergency stop bypasses the page request filter on its dedicated port');
assert.match(source,
  /const CONTROL_REQUEST_TIMEOUT_MS = \d+;[\s\S]*function fetchWithTimeout\([\s\S]*controller\.abort\(\)/,
  'control requests have a bounded browser-side timeout');
assert.match(source,
  /function recoverExpiredLease\([\s\S]*webRCLeaseToken = ''[\s\S]*webRCLeaseBlocked = false/,
  'an expired lease is cleared without permanently blocking recovery');
assert.match(source,
  /web_rc_lease_in_use[\s\S]*handleLeaseConflict\('遥控控制权已被其他页面占用'/,
  'only an explicit active-owner response is shown as occupied');
assert.match(source,
  /let joystickRequestPromise = null;[\s\S]*let joystickSendPending = false;[\s\S]*while \(joystickSendPending\)/,
  'joystick requests are serialized and coalesce to the latest stick state');
assert.match(source,
  /let selfCheckRequestInFlight = false;[\s\S]*function loadSelfCheckStatus\(showLoading\)[\s\S]*if \(selfCheckRequestInFlight\) return/,
  'status polling keeps at most one request in flight');
assert.match(source,
  /setInterval\(\(\) => \{[\s\S]*if \(heartbeatRequestPromise\) return;[\s\S]*sendToESP\('\/web_rc\/heartbeat'/,
  'heartbeats continue probing after a temporary disconnect without accumulating');
assert.match(source,
  /function releaseControlsForPageExit\(\)[\s\S]*rightStick=\{x:0,y:0,rawX:0,rawY:0\}[\s\S]*leftStick\.rawY=hoverThrottleRaw[\s\S]*sendJoystickData\(\)[\s\S]*stickInputActivated=false/,
  'leaving the page sends neutral attitude and hover throttle once, then stops stale stick renewal');
for (const eventName of ['visibilitychange', 'pagehide', 'blur']) {
  assert.ok(source.includes(`addEventListener('${eventName}'`),
    `the controller handles ${eventName} lifecycle loss`);
}
assert.match(source,
  /id="calibration-readiness"[\s\S]*陀螺静止校准、六面加速度计校准和机身水平校准/,
  'the preflight page has a dedicated calibration checklist');
assert.match(source,
  /gyro_bias_ready[\s\S]*accel_calibration_stored[\s\S]*level_calibration_stored/,
  'the preflight checklist renders all required calibration states');
assert.match(source,
  /id="barometer-status"[\s\S]*barometer_guard_ready[\s\S]*气压计为可选传感器，不影响基础手动解锁/,
  'the preflight page exposes optional barometer guard health and its fallback');
assert.match(source,
  /DESCENT_CALIBRATION_MAX_AGE_MS = 30\*24\*60\*60\*1000[\s\S]*device_id===currentDeviceId[\s\S]*firmware_build===currentFirmwareBuild[\s\S]*point\.airframe===config\.airframe[\s\S]*point\.prop===config\.prop/,
  'descent calibration recommendations reject expired or mismatched device, build, airframe, and propeller data');
assert.match(source,
  /applyDescentCalibrationRecommendation\(\)[\s\S]*fetchWithTimeout\('\/web_rc\/status'[\s\S]*descentPointMatch\(descentCalibrationRecommendation,config\)[\s\S]*stillStored/,
  'descent calibration save revalidates live identity, configuration, age, and stored recommendation');
assert.match(source,
  /PID 查看与调整：[\s\S]*p CTL_R_RATE_P[\s\S]*p CTL_R_RATE_P 0\.06/,
  'the Web debug console explains how to inspect and change PID parameters');

assert.match(source,
  /async function openConsole\(\)[\s\S]*fetch\('\/web_rc\/status'[\s\S]*flightStatus\.armed/,
  'opening debug actively verifies the current armed state');
assert.doesNotMatch(source,
  /open && \(!armedStatusKnown \|\| currentArmed\)/,
  'stale armedStatusKnown no longer makes the debug button silently inert');
assert.match(source,
  /if\(!response\.ok\|\|!resp\.ok\)[\s\S]*throw new Error/,
  'command HTTP failures are surfaced');
assert.match(source,
  /if\(input\.value\.trim\(\)===cmd\)input\.value=''/,
  'the command input is cleared only after a successful response');
assert.doesNotMatch(source,
  /controlFetch\('\/console\/(?:enable|disable|cmd)'[\s\S]{0,180}\.catch\(\(\)=>\{\}\)/,
  'console operations do not silently swallow failures');

console.log('web debug console UI contract checks passed');
