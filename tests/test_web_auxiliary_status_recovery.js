// WEB-RECOVER-1: auxiliary GET polling shares one in-flight request per endpoint.
// WEB-RECOVER-2: the deadline covers the response body; failures release the slot
// and a later successful request is parsed once and available to every caller.
// One local increment covers the controller status readers and recovery polling;
// Wi-Fi/HTTP service changes remain owned by the main thread.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '..', 'web_rc_html_controller.h'), 'utf8');
const timeoutStart = source.indexOf('async function fetchWithTimeout(');
const timeoutEnd = source.indexOf('\nfunction recoverExpiredLease', timeoutStart);
const readerStart = source.indexOf('const auxiliaryStatusRequests = new Map();');
const readerEnd = source.indexOf('\nasync function refreshDescentCalibrationStatus()', readerStart);
assert.ok(readerStart >= 0 && readerEnd > readerStart, 'bounded auxiliary reader exists');
const implementation = source.slice(timeoutStart, timeoutEnd) + '\n' + source.slice(readerStart, readerEnd);

async function run() {
  let mode = 'pending', calls = 0, parses = 0, finish;
  const context = {AbortController, setTimeout, clearTimeout,
    STATUS_REQUEST_TIMEOUT_MS: 25,
    fetch: async (_url, options) => {
      calls++;
      if(mode === 'abort') throw Object.assign(new Error('aborted'), {name: 'AbortError'});
      const body = () => mode === 'pending' ? new Promise((resolve, reject) => {
        finish = resolve;
        options.signal.addEventListener('abort', () => reject(new Error('body timeout')), {once: true});
      }) : Promise.resolve(new ArrayBuffer(0));
      return {ok: mode !== 'http-error', clone: () => ({arrayBuffer: body}),
        json: async () => {parses++; if (mode === 'invalid-json') throw new Error('invalid JSON'); return {state: 'ready'};}};
    }};
  vm.runInNewContext(implementation, context);
  const a = context.fetchAuxiliaryStatus('/level-calibration/status');
  const b = context.fetchAuxiliaryStatus('/level-calibration/status');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(calls, 1, 'overlapping polls use one network request');
  finish(new ArrayBuffer(0));
  const values = await Promise.all([a, b]);
  assert.strictEqual(values[0], values[1], 'shared response is parsed once');
  assert.equal(parses, 1);

  mode = 'pending';
  await assert.rejects(context.fetchAuxiliaryStatus('/route/status'), /body timeout/);
  mode = 'success';
  assert.equal((await context.fetchAuxiliaryStatus('/route/status')).state, 'ready', 'timeout does not permanently block recovery');
  mode = 'abort';
  await assert.rejects(context.fetchAuxiliaryStatus('/route/status'), /状态读取超时/);
  mode = 'http-error';
  await assert.rejects(context.fetchAuxiliaryStatus('/route/status'), /状态读取失败/);
  mode = 'invalid-json';
  await assert.rejects(context.fetchAuxiliaryStatus('/route/status'), /invalid JSON/);
  mode = 'success';
  await context.fetchAuxiliaryStatus('/route/status');
  const before = calls;
  await Promise.all([context.fetchAuxiliaryStatus('/level-calibration/status'), context.fetchAuxiliaryStatus('/vibration-calibration/status')]);
  assert.equal(calls - before, 2, 'different endpoints do not share stale data');
  for (const route of ['/descent-calibration/status', '/level-calibration/status', '/vibration-calibration/status', '/route/status'])
    assert.ok(source.includes(`await fetchAuxiliaryStatus('${route}')`), route + ': production reader uses deadline and single-flight');
}
run().then(() => console.log('auxiliary status timeout, single-flight and recovery: PASS')).catch(error => {console.error(error); process.exitCode = 1;});
