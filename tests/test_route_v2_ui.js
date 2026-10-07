const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'web_rc_html_controller.h'), 'utf8');

assert.match(source, /const ROUTE_RECORD_HEADER_V2='# WEB_RC_RECORDED_V2'/,
  'navigation recordings carry the V2 schema marker');
assert.match(source, /const ROUTE_RECORD_HEADER_V1='# WEB_RC_RECORDED_V1'/,
  'non-navigation recordings remain explicit V1');
assert.match(source,
  /const \[duration,throttle,roll,pitch,yaw,altitude,heading\]=fields\.map\(Number\)/,
  'the editor parses height and heading after the five legacy controls');
assert.match(source,
  /routeNavigationSample\.valid[\s\S]*receivedAt<=350[\s\S]*altitude:navigationFresh[\s\S]*heading:navigationFresh/,
  'armed recording only commits fresh board navigation samples');
assert.match(source,
  /formatRouteRecordLine\(segment\)[\s\S]*segment\.altitude\.toFixed\(3\)[\s\S]*segment\.heading\.toFixed\(2\)/,
  'V2 lines retain high resolution height and heading');
assert.match(source,
  /segments\.every\(segment=>segment\.altitude!==null&&segment\.heading!==null\)/,
  'recording output chooses one schema for every row');
assert.match(source,
  /value\.altitude===null\|\|value\.heading===null[\s\S]*stopRouteRecording/,
  'armed V2 recording stops before writing a stale navigation row');
assert.match(source,
  /routeRecording&&routeRecordStartedArmed\?ROUTE_RECORD_SAMPLE_MS:FORCE_SEND_INTERVAL/,
  'armed recording requests a board snapshot at the 10 Hz recording cadence');
assert.match(source,
  /resp\.nav===true[\s\S]*altitude:Number\(resp\.alt\)[\s\S]*heading:Number\(resp\.hdg\)/,
  'the recorder consumes fused height and attitude heading returned by the flight controller');
assert.match(source, /schema=2 source=authored policy=slew advance=arrival heading=relative/,
  'the editor accepts arrival-gated authored V2 routes');
assert.match(source, /data\.phase==='waiting_arrival'\|\|data\.phase==='stabilizing'/,
  'the route page reports arrival and stabilization progress');
assert.match(source, /arrival_timeout:'未在限定时间到达目标高度，已转下降'/,
  'the route page explains arrival timeout fallback');

console.log('route V2 recording UI contracts passed');
