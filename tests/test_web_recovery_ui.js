const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'web_rc_recovery_html.h'), 'utf8');
const firmware = fs.readFileSync(path.join(__dirname, '..', 'web_rc.ino'), 'utf8');

assert.match(source, /cfDroneStopToken[\s\S]*\/web_rc\/lease[\s\S]*web_rc_flight_takeover_forbidden/,
  'the recovery page uses the persisted continuity token and surfaces forbidden takeover');
assert.match(source, /joystick-left[\s\S]*joystick-right[\s\S]*sendLatest/,
  'the recovery page retains both flight sticks and serialized latest-value sending');
assert.match(source, /:82\/\$\{action\}[\s\S]*mode:'no-cors'/,
  'the recovery page keeps the independent emergency path');
assert.match(source, /visibilitychange[\s\S]*pagehide[\s\S]*blur/,
  'the recovery page releases controls on lifecycle loss');
assert.match(source, /async function timeoutFetch[\s\S]*clone\(\)\.arrayBuffer\(\)[\s\S]*finally\{clearTimeout\(t\)\}/,
  'the recovery request deadline remains active through response body consumption');
assert.match(source, /web_rc_lease_in_use'[\s\S]*leaseBlocked=true[\s\S]*if\(leaseBlocked\)return/,
  'a recovery page displaced by another controller stops reacquiring its lease');
assert.match(source, /if\(active\)\{sendLatest\(\);return\}/,
  'active recovery controls periodically renew the latest stick values');
assert.match(source, /location\.reload\(\)/,
  'the full controller can be restored after the aircraft is locked');
assert.match(firmware,
  /webRCServer\.on\("\/", HTTP_GET[\s\S]*if \(armed \|\| motorsActive\(\)\)[\s\S]*Cache-Control[\s\S]*webRCRecoveryHtml/,
  'armed root requests receive the bounded recovery page instead of the full controller');
assert.ok(Buffer.byteLength(source, 'utf8') <= 16 * 1024,
  'the recovery page remains within its small response budget');

console.log('web recovery UI contracts passed');
