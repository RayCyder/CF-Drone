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
  'time', 'sys', 'log status', 'p', 'help']) {
  assert.ok(source.includes(`runConsoleCommand('${command}')`),
    `common command button exists for ${command}`);
}

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
